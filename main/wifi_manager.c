#include <string.h>
#include "wifi_manager.h"
#include "config_store.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "ws2812.h"

static const char *TAG = "wifi_mgr";
static EventGroupHandle_t s_evt;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static volatile bool s_connected = false;
static int s_retry = 0;
#define MAX_RETRY 5

#define AP_SSID "ESP32-PS5-Setup"

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        /* Stay deterministic: once STA fails and AP starts, AP mode becomes yellow. */
        ws2812_set_wifi_status(false, false);
        if (s_retry < MAX_RETRY) {
            esp_wifi_connect();
            s_retry++;
            ESP_LOGI(TAG, "retry connect to AP (%d/%d)", s_retry, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_evt, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry = 0;
        s_connected = true;
        app_config_t cfg;
        config_store_load(&cfg);
        ws2812_set_wifi_status(true, cfg.registered);
        xEventGroupSetBits(s_evt, WIFI_CONNECTED_BIT);
    }
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

static esp_err_t start_sta(const app_config_t *cfg)
{
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, cfg->wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, cfg->wifi_pass, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_evt, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "STA connected to %s", cfg->wifi_ssid);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "STA connect failed/timeout");
    return ESP_FAIL;
}

static esp_err_t start_ap(void)
{
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    app_config_t cfg;
    config_store_load(&cfg);
    const char *ap_pass = (cfg.ap_pass[0] && strlen(cfg.ap_pass) >= 8) ? cfg.ap_pass : CFG_DEFAULT_AP_PASS;

    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(AP_SSID),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)ap_config.ap.ssid, AP_SSID, sizeof(ap_config.ap.ssid) - 1);
    strncpy((char *)ap_config.ap.password, ap_pass, sizeof(ap_config.ap.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ws2812_set_ap_mode();
    ESP_LOGI(TAG, "AP started: SSID=%s PASS=%s (portal http://192.168.4.1)", AP_SSID, ap_pass);
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    s_evt = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    app_config_t cfg;
    config_store_load(&cfg);

    if (strlen(cfg.wifi_ssid) > 0) {
        if (start_sta(&cfg) == ESP_OK) {
            return ESP_OK;
        }
        // If the connection failed, fall back to AP mode so settings can be fixed
        esp_wifi_stop();
        esp_wifi_deinit();
    }
    return start_ap();
}

esp_err_t wifi_manager_set_performance_mode(bool enable)
{
    wifi_ps_type_t ps = enable ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM;
    esp_err_t err = esp_wifi_set_ps(ps);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_ps(%s) failed: %s",
                 enable ? "NONE" : "MIN_MODEM", esp_err_to_name(err));
    }
    return err;
}
