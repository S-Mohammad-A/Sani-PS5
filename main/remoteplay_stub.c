#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "remoteplay_stub.h"
#include "input_config.h"
#include "bluepad32_bridge.h"
#include "web_portal.h"
#include "wifi_manager.h"
#include "ws2812.h"
#include "controller/uni_gamepad.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <chiaki/discovery.h>
#include <chiaki/regist.h>
#include <chiaki/log.h>
#include <chiaki/base64.h>
#include <chiaki/session.h>

static const char *TAG="remoteplay";
volatile bool g_input_profile_dirty = false;
static rp_lightbar_cb_t s_lightbar_cb;
static SemaphoreHandle_t s_discovery_done,s_regist_done;
static ChiakiDiscoveryHostState s_discovery_state;
static bool s_discovery_is_ps5,s_regist_success;
static ChiakiRegisteredHost s_registered_host;
static ChiakiSession s_session;
static bool s_session_initialized;
static atomic_bool s_controller_input_enabled;
static SemaphoreHandle_t s_session_lock;
static atomic_bool s_cleanup_running;

static void chiaki_log_to_esp(ChiakiLogLevel level,const char*msg,void*user){
    (void)user;
    if(level==CHIAKI_LOG_ERROR)ESP_LOGE(TAG,"%s",msg);
    else if(level==CHIAKI_LOG_WARNING)ESP_LOGW(TAG,"%s",msg);
    else if(level==CHIAKI_LOG_INFO)ESP_LOGI(TAG,"%s",msg);
    else ESP_LOGD(TAG,"%s",msg);
}
static ChiakiLog s_log;

static void discovery_cb(ChiakiDiscoveryHost*h,void*u){
    (void)u;s_discovery_state=h->state;s_discovery_is_ps5=chiaki_discovery_host_is_ps5(h);
    ESP_LOGI(TAG,"Discovery: state=%s PS5=%d host=%s",
        chiaki_discovery_host_state_string(h->state),s_discovery_is_ps5,
        h->host_name?h->host_name:"?");
    xSemaphoreGive(s_discovery_done);
}
static bool run_discovery(const char*host){
    ChiakiDiscovery d;ChiakiErrorCode e=chiaki_discovery_init(&d,&s_log,AF_INET);
    if(e!=CHIAKI_ERR_SUCCESS)return false;
    ChiakiDiscoveryThread th;
    e=chiaki_discovery_thread_start_oneshot(&th,&d,discovery_cb,NULL);
    if(e!=CHIAKI_ERR_SUCCESS){chiaki_discovery_fini(&d);return false;}
    struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons(CHIAKI_DISCOVERY_PORT_PS5);
    if(inet_pton(AF_INET,host,&a.sin_addr)!=1){chiaki_discovery_thread_stop(&th);chiaki_discovery_fini(&d);return false;}
    ChiakiDiscoveryPacket p={0};p.cmd=CHIAKI_DISCOVERY_CMD_SRCH;p.protocol_version=CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5;
    e=chiaki_discovery_send(&d,&p,(struct sockaddr*)&a,sizeof(a));
    bool ok=e==CHIAKI_ERR_SUCCESS && xSemaphoreTake(s_discovery_done,pdMS_TO_TICKS(3000))==pdTRUE && s_discovery_is_ps5;
    chiaki_discovery_thread_stop(&th);chiaki_discovery_fini(&d);return ok;
}
static void regist_cb(ChiakiRegistEvent*e,void*u){
    (void)u;
    if(e->type==CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS){
        s_registered_host=*e->registered_host;s_regist_success=true;
        ESP_LOGI(TAG,"Registration OK, nickname=%s",s_registered_host.server_nickname);
    }else{s_regist_success=false;ESP_LOGE(TAG,"Registration failed/canceled");}
    xSemaphoreGive(s_regist_done);
}
static bool run_registration(const char*host,const char*account_b64,const char*pin, ChiakiRegisteredHost *out_host){
    uint8_t account[CHIAKI_PSN_ACCOUNT_ID_SIZE];size_t n=sizeof(account);
    if(chiaki_base64_decode(account_b64,strlen(account_b64),account,&n)!=CHIAKI_ERR_SUCCESS||n!=sizeof(account)){
        ESP_LOGE(TAG,"Invalid PSN Account ID");return false;
    }
    ChiakiRegistInfo info={0};info.target=CHIAKI_TARGET_PS5_1;info.host=host;info.broadcast=false;
    memcpy(info.psn_account_id,account,sizeof(account));info.pin=(uint32_t)strtoul(pin,NULL,10);
    s_regist_success=false;ChiakiRegist r;
    ChiakiErrorCode e=chiaki_regist_start(&r,&s_log,&info,regist_cb,NULL);
    if(e!=CHIAKI_ERR_SUCCESS)return false;
    if(xSemaphoreTake(s_regist_done,pdMS_TO_TICKS(10000))!=pdTRUE){
        chiaki_regist_stop(&r);xSemaphoreTake(s_regist_done,pdMS_TO_TICKS(2000));
    }
    chiaki_regist_fini(&r);
    if (s_regist_success && out_host)
        *out_host = s_registered_host;
    return s_regist_success;
}

/* QUIT runs inside session thread — cannot join/fini there. Deferred task
 * stops residual Takion/Ctrl traffic, frees the session, then starts portal. */
static void session_cleanup_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(300));
    if (xSemaphoreTake(s_session_lock, pdMS_TO_TICKS(8000)) == pdTRUE) {
        if (s_session_initialized) {
            atomic_store_explicit(&s_controller_input_enabled, false, memory_order_release);
            chiaki_session_stop(&s_session);
            chiaki_session_join(&s_session);
            chiaki_session_fini(&s_session);
            s_session_initialized = false;
            ESP_LOGI(TAG, "Session torn down after quit");
        }
        xSemaphoreGive(s_session_lock);
    }
    wifi_manager_set_performance_mode(false);
    ws2812_set_remote_connected(false);
    web_portal_start();
    atomic_store_explicit(&s_cleanup_running, false, memory_order_release);
    vTaskDelete(NULL);
}

static void session_event(ChiakiEvent*e,void*u){
    (void)u;
    if(e->type==CHIAKI_EVENT_LED_COLOR) {
        ws2812_set_console_color(e->led_state[0], e->led_state[1], e->led_state[2]);
        if(s_lightbar_cb)
            s_lightbar_cb(e->led_state[0],e->led_state[1],e->led_state[2]);
    }
    else if(e->type==CHIAKI_EVENT_RUMBLE) {
        ESP_LOGD(TAG,"PS5 rumble L=%u R=%u",e->rumble.left,e->rumble.right);
        bluepad32_bridge_play_rumble(e->rumble.right, e->rumble.left);
    }
    else if(e->type==CHIAKI_EVENT_PLAYER_INDEX)
        ESP_LOGD(TAG,"PS5 player index=%u",e->player_index);
    else if(e->type==CHIAKI_EVENT_CONNECTED) {
        ws2812_set_remote_connected(true);
        ESP_LOGI(TAG,"Remote Play connected");
        atomic_store_explicit(&s_controller_input_enabled, true, memory_order_release);
    }
    else if(e->type==CHIAKI_EVENT_QUIT) {
        ESP_LOGW(TAG,"Remote Play quit: %d %s", (int)e->quit.reason,
                 e->quit.reason_str ? e->quit.reason_str : "");
        atomic_store_explicit(&s_controller_input_enabled, false, memory_order_release);
        ws2812_set_remote_connected(false);
        bool expected = false;
        if (atomic_compare_exchange_strong_explicit(&s_cleanup_running, &expected, true,
                                                    memory_order_acq_rel, memory_order_acquire)) {
            xTaskCreate(session_cleanup_task, "rp_cleanup", 4096, NULL, 5, NULL);
        }
    }
}

esp_err_t remoteplay_stub_init(rp_lightbar_cb_t cb){
    s_lightbar_cb=cb;
    /* WARNING|ERROR only — INFO heartbeats/hex dumps starve WiFi under BLE coexist */
    chiaki_log_init(&s_log, CHIAKI_LOG_WARNING | CHIAKI_LOG_ERROR, chiaki_log_to_esp, NULL);
    s_discovery_done=xSemaphoreCreateBinary();s_regist_done=xSemaphoreCreateBinary();
    s_session_lock=xSemaphoreCreateMutex();
    atomic_store_explicit(&s_cleanup_running, false, memory_order_relaxed);
    return s_discovery_done&&s_regist_done&&s_session_lock?ESP_OK:ESP_ERR_NO_MEM;
}

esp_err_t remoteplay_stub_register(const char *host, const char *account, const char *pin, ChiakiRegisteredHost *out_host)
{
    if (!host || !account || !pin || !*host || !*account || !*pin)
        return ESP_ERR_INVALID_ARG;
    if (!run_discovery(host)) {
        ESP_LOGE(TAG, "PS5 discovery failed");
        return ESP_FAIL;
    }
    if (!run_registration(host, account, pin, out_host))
        return ESP_FAIL;
    return ESP_OK;
}

esp_err_t remoteplay_stub_connect_registered(const char *host, const uint8_t *rp_regist_key, const uint8_t *rp_key)
{
    if (!host || !rp_regist_key || !rp_key || !*host)
        return ESP_ERR_INVALID_ARG;
    if(!s_session_lock) return ESP_ERR_INVALID_STATE;
    if(xSemaphoreTake(s_session_lock, pdMS_TO_TICKS(5000)) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    wifi_manager_set_performance_mode(true);

    if (s_session_initialized) {
        atomic_store_explicit(&s_controller_input_enabled, false, memory_order_release);
        chiaki_session_stop(&s_session);
        chiaki_session_join(&s_session);
        chiaki_session_fini(&s_session);
        s_session_initialized = false;
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (!run_discovery(host)) {
        ESP_LOGE(TAG, "PS5 discovery failed");
        wifi_manager_set_performance_mode(false);
        xSemaphoreGive(s_session_lock);
        return ESP_FAIL;
    }

    ChiakiConnectInfo ci = {0};
    ci.ps5 = true;
    ci.host = host;
    memcpy(ci.regist_key, rp_regist_key, sizeof(ci.regist_key));
    memcpy(ci.morning, rp_key, sizeof(ci.morning));
    ci.audio_video_disabled = CHIAKI_AUDIO_VIDEO_DISABLED;
    ci.enable_dualsense = true;
    ci.enable_keyboard = false;

    ChiakiErrorCode e = chiaki_session_init(&s_session, &ci, &s_log);
    if (e != CHIAKI_ERR_SUCCESS) {
        ESP_LOGE(TAG, "session init: code=%d (%s)", (int)e, chiaki_error_string(e));
        wifi_manager_set_performance_mode(false);
        xSemaphoreGive(s_session_lock);
        return ESP_FAIL;
    }

    s_session.event_cb = session_event;
    s_session.event_cb_user = NULL;
    e = chiaki_session_start(&s_session);
    if (e != CHIAKI_ERR_SUCCESS) {
        chiaki_session_fini(&s_session);
        wifi_manager_set_performance_mode(false);
        xSemaphoreGive(s_session_lock);
        return ESP_FAIL;
    }

    s_session_initialized = true;
    atomic_store_explicit(&s_controller_input_enabled, false, memory_order_release);
    xSemaphoreGive(s_session_lock);
    return ESP_OK;
}

void remoteplay_stub_send_controller_state(const rp_controller_state_t*in){
    if(!in || !s_session_lock) return;
    if(xSemaphoreTake(s_session_lock, pdMS_TO_TICKS(100)) != pdTRUE) return;
    if(!s_session_initialized || !atomic_load_explicit(&s_controller_input_enabled, memory_order_acquire)){
        xSemaphoreGive(s_session_lock);
        return;
    }
    ChiakiControllerState st;chiaki_controller_state_set_idle(&st);
    /* Drift/remap already applied in bluepad32_bridge — pass through only. */
    st.buttons = in->buttons & 0xFFFFu;
    st.left_x=in->left_x;st.left_y=in->left_y;
    st.right_x=in->right_x;st.right_y=in->right_y;
    st.l2_state=in->l2;st.r2_state=in->r2;

    static ChiakiControllerState last_sent = {0};
    static bool have_last = false;
    static int64_t last_send_us = 0;
    int64_t now_us = esp_timer_get_time();

    st.left_x  = (int16_t)(st.left_x  & ~0x3F);
    st.left_y  = (int16_t)(st.left_y  & ~0x3F);
    st.right_x = (int16_t)(st.right_x & ~0x3F);
    st.right_y = (int16_t)(st.right_y & ~0x3F);
    st.l2_state = (uint8_t)(st.l2_state & ~0x07);
    st.r2_state = (uint8_t)(st.r2_state & ~0x07);

    bool buttons = !have_last || st.buttons != last_sent.buttons;
    bool triggers = !have_last
        || abs((int)st.l2_state - (int)last_sent.l2_state) >= 8
        || abs((int)st.r2_state - (int)last_sent.r2_state) >= 8;
    bool sticks = !have_last
        || abs((int)st.left_x - (int)last_sent.left_x) > 128
        || abs((int)st.left_y - (int)last_sent.left_y) > 128
        || abs((int)st.right_x - (int)last_sent.right_x) > 128
        || abs((int)st.right_y - (int)last_sent.right_y) > 128;

    if(!buttons && !triggers && !sticks) {
        xSemaphoreGive(s_session_lock);
        return;
    }
    int64_t min_us = buttons ? 20000 : 66000;
    if((now_us - last_send_us) < min_us) {
        xSemaphoreGive(s_session_lock);
        return;
    }
    last_sent = st;
    have_last = true;
    last_send_us = now_us;

    (void)chiaki_session_set_controller_state(&s_session, &st);
    xSemaphoreGive(s_session_lock);
}
