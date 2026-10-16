#include "bluepad32_bridge.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include "remoteplay_stub.h"
#include "config_store.h"
#include "input_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "btstack_run_loop.h"
#include "uni_hid_device.h"
#include "bt/uni_bt.h"

static const char *TAG = "bp32_bridge";
static uni_hid_device_t *s_controller_device;
static uint8_t s_battery_level;
static uint8_t s_dz_lx, s_dz_ly, s_dz_rx, s_dz_ry;
static bool s_inv_lx, s_inv_ly, s_inv_rx, s_inv_ry;
static uint8_t s_lt_dz, s_rt_dz;
static uint16_t s_lt_idle, s_rt_idle, s_lt_max, s_rt_max;
static input_profile_t s_in_profile;

#define RUMBLE_MIN_INTERVAL_US  33000ULL
#define RUMBLE_PULSE_MS         120
#define RUMBLE_STOP_MS          10

static volatile uint8_t s_rumble_weak;
static volatile uint8_t s_rumble_strong;
static volatile uint8_t s_peak_weak;
static volatile uint8_t s_peak_strong;
static atomic_bool s_rumble_pending;
static atomic_bool s_rumble_scheduled;
static int64_t s_last_rumble_us;
static bool s_rumble_enabled = true;
static btstack_context_callback_registration_t s_rumble_callback;

static uint8_t rumble_gain(uint8_t x)
{
    uint16_t boost = ((uint16_t)x * (255U - x)) / 512U;
    uint16_t y = (uint16_t)x + boost;
    return (y > 255U) ? 255U : (uint8_t)y;
}

static void rumble_btstack_callback(void *context)
{
    (void)context;
    atomic_store_explicit(&s_rumble_scheduled, false, memory_order_release);
    if (!atomic_exchange_explicit(&s_rumble_pending, false, memory_order_acq_rel))
        return;

    uni_hid_device_t *d = s_controller_device;
    if (!d || !d->report_parser.play_dual_rumble)
        return;

    uint8_t weak = s_rumble_weak;
    uint8_t strong = s_rumble_strong;
    s_peak_weak = 0;
    s_peak_strong = 0;

    if (weak == 0 && strong == 0) {
        d->report_parser.play_dual_rumble(d, 0, RUMBLE_STOP_MS, 0, 0);
        return;
    }
    d->report_parser.play_dual_rumble(d, 0, RUMBLE_PULSE_MS, weak, strong);
}

void bluepad32_bridge_set_rumble_enabled(bool enabled)
{
    s_rumble_enabled = enabled;
    if (!enabled && s_controller_device && s_controller_device->report_parser.play_dual_rumble) {
        s_rumble_weak = 0;
        s_rumble_strong = 0;
        s_peak_weak = 0;
        s_peak_strong = 0;
        atomic_store_explicit(&s_rumble_pending, true, memory_order_release);
        bool expected = false;
        if (atomic_compare_exchange_strong_explicit(&s_rumble_scheduled, &expected, true,
                                                    memory_order_acq_rel, memory_order_acquire)) {
            s_rumble_callback.callback = rumble_btstack_callback;
            s_rumble_callback.context = NULL;
            btstack_run_loop_execute_on_main_thread(&s_rumble_callback);
        }
    }
}

bool bluepad32_bridge_get_rumble_enabled(void)
{
    return s_rumble_enabled;
}

const char *bluepad32_bridge_get_controller_name(void)
{
    if (!s_controller_device)
        return "None";
    if (s_controller_device->name[0])
        return s_controller_device->name;
    return "Xbox One / Series";
}

int bluepad32_bridge_get_battery(void)
{
    if (!s_controller_device)
        return -1;
    if (s_battery_level == 0 || s_battery_level == 255)
        return -1;
    return (int)((s_battery_level * 100) / 254);
}

void bluepad32_bridge_play_rumble(uint8_t weak_magnitude, uint8_t strong_magnitude)
{
    if (!s_controller_device || !s_rumble_enabled)
        return;

    weak_magnitude = rumble_gain(weak_magnitude);
    strong_magnitude = rumble_gain(strong_magnitude);

    if (weak_magnitude > s_peak_weak)
        s_peak_weak = weak_magnitude;
    if (strong_magnitude > s_peak_strong)
        s_peak_strong = strong_magnitude;

    int64_t now = esp_timer_get_time();
    bool is_stop = (weak_magnitude == 0 && strong_magnitude == 0);
    if (!is_stop && (now - s_last_rumble_us < (int64_t)RUMBLE_MIN_INTERVAL_US))
        return;

    s_last_rumble_us = now;
    s_rumble_weak = is_stop ? 0 : s_peak_weak;
    s_rumble_strong = is_stop ? 0 : s_peak_strong;
    atomic_store_explicit(&s_rumble_pending, true, memory_order_release);

    bool expected = false;
    if (atomic_compare_exchange_strong_explicit(&s_rumble_scheduled, &expected, true,
                                                memory_order_acq_rel, memory_order_acquire)) {
        s_rumble_callback.callback = rumble_btstack_callback;
        s_rumble_callback.context = NULL;
        btstack_run_loop_execute_on_main_thread(&s_rumble_callback);
    }
}

void bluepad32_bridge_reload_input_tuning(void)
{
    input_config_load(&s_in_profile);
    uint8_t g = s_in_profile.dz_global;
    s_dz_lx = s_in_profile.dz_lx ? s_in_profile.dz_lx : g;
    s_dz_ly = s_in_profile.dz_ly ? s_in_profile.dz_ly : g;
    s_dz_rx = s_in_profile.dz_rx ? s_in_profile.dz_rx : g;
    s_dz_ry = s_in_profile.dz_ry ? s_in_profile.dz_ry : g;
    s_inv_lx = s_in_profile.inv_lx;
    s_inv_ly = s_in_profile.inv_ly;
    s_inv_rx = s_in_profile.inv_rx;
    s_inv_ry = s_in_profile.inv_ry;
    s_lt_dz = s_in_profile.lt_dz;
    s_rt_dz = s_in_profile.rt_dz;
    s_lt_idle = s_in_profile.lt_idle;
    s_rt_idle = s_in_profile.rt_idle;
    s_lt_max = s_in_profile.lt_max ? s_in_profile.lt_max : 1023;
    s_rt_max = s_in_profile.rt_max ? s_in_profile.rt_max : 1023;
    app_config_t cfg;
    if (config_store_load(&cfg) == ESP_OK)
        s_rumble_enabled = cfg.rumble_enabled;
    else
        s_rumble_enabled = true;
    extern volatile bool g_input_profile_dirty;
    g_input_profile_dirty = false;
}

static void platform_init(int argc, const char **argv)
{
    (void)argc; (void)argv;
    ESP_LOGI(TAG, "bluepad32 init");
    bluepad32_bridge_reload_input_tuning();
}

static void platform_on_init_complete(void)
{
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);
}

static uni_error_t platform_on_device_discovered(bd_addr_t addr, const char *name, uint16_t cod, uint8_t rssi)
{
    (void)addr; (void)name; (void)cod; (void)rssi;
    return UNI_ERROR_SUCCESS;
}

static void platform_on_device_connected(uni_hid_device_t *d)
{
    (void)d;
    ESP_LOGI(TAG, "controller connected");
}

static void platform_on_device_disconnected(uni_hid_device_t *d)
{
    if (d == s_controller_device) {
        s_controller_device = NULL;
        s_battery_level = 0;
    }
    ESP_LOGW(TAG, "controller disconnected");
    uni_bt_start_scanning_and_autoconnect_safe();
}

static uni_error_t platform_on_device_ready(uni_hid_device_t *d)
{
    s_controller_device = d;
    ESP_LOGI(TAG, "controller ready; stop BT scan");
    uni_bt_stop_scanning_safe();
    return UNI_ERROR_SUCCESS;
}

/* Single-pass: deadzone + invert + triggers + button remap applied HERE
 * when Bluepad32 delivers a sample. remoteplay_stub only forwards. */
static void platform_on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl)
{
    (void)d;
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) return;
    uni_gamepad_t *gp = &ctl->gamepad;
    s_battery_level = ctl->battery;

    extern volatile bool g_input_profile_dirty;
    if (g_input_profile_dirty) {
        bluepad32_bridge_reload_input_tuning();
    }

    uint32_t mapped = input_config_map_buttons(gp->buttons, gp->dpad, gp->misc_buttons, &s_in_profile);

    rp_controller_state_t state = {
        .buttons      = mapped,
        .dpad         = 0,
        .misc_buttons = 0,
        .left_x       = (int16_t)(input_config_apply_axis(gp->axis_x,  s_dz_lx, s_inv_lx) * 64),
        .left_y       = (int16_t)(input_config_apply_axis(gp->axis_y,  s_dz_ly, s_inv_ly) * 64),
        .right_x      = (int16_t)(input_config_apply_axis(gp->axis_rx, s_dz_rx, s_inv_rx) * 64),
        .right_y      = (int16_t)(input_config_apply_axis(gp->axis_ry, s_dz_ry, s_inv_ry) * 64),
        .l2           = input_config_apply_trigger((uint16_t)gp->brake, s_lt_dz, s_lt_idle, s_lt_max),
        .r2           = input_config_apply_trigger((uint16_t)gp->throttle, s_rt_dz, s_rt_idle, s_rt_max),
    };
    remoteplay_stub_send_controller_state(&state);
}

static const uni_property_t *platform_get_property(uni_property_idx_t idx)
{
    (void)idx;
    return NULL;
}

static void platform_on_oob_event(uni_platform_oob_event_t event, void *data)
{
    (void)event; (void)data;
}

struct uni_platform *bluepad32_bridge_get_platform(void)
{
    static struct uni_platform platform = {
        .name                   = "esp32-ps5-remote",
        .init                   = platform_init,
        .on_init_complete       = platform_on_init_complete,
        .on_device_discovered   = platform_on_device_discovered,
        .on_device_connected    = platform_on_device_connected,
        .on_device_disconnected = platform_on_device_disconnected,
        .on_device_ready        = platform_on_device_ready,
        .on_controller_data     = platform_on_controller_data,
        .get_property           = platform_get_property,
        .on_oob_event           = platform_on_oob_event,
    };
    return &platform;
}
