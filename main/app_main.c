#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "uni.h"

#include "config_store.h"
#include "wifi_manager.h"
#include "web_portal.h"
#include "ws2812.h"
#include "remoteplay_stub.h"
#include "bluepad32_bridge.h"
#include "connect_button.h"
#include "factory_reset_button.h"

static const char *TAG = "app_main";

static void on_lightbar_color(uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_set_color(r, g, b);
}

void app_main(void)
{
    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(ws2812_init());
    ws2812_set_color(20, 0, 0); // dim red = not connected yet

    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(web_portal_start());

    ESP_ERROR_CHECK(remoteplay_stub_init(on_lightbar_color));
    ESP_ERROR_CHECK(connect_button_init());
    ESP_ERROR_CHECK(factory_reset_button_init());

    app_config_t cfg;
    config_store_load(&cfg);

    // Connecting is intentionally manual. The portal's Save & Register step
    // performs the one-time registration and stores the returned RP keys;
    // the Connect button later starts Remote Play using those stored keys.
    if (wifi_manager_is_connected()) {
        ws2812_set_wifi_status(true, cfg.registered);
    } else {
        ESP_LOGI(TAG, "Waiting for user setup via the web portal "
                       "(connect to the ESP32-PS5-Setup AP and open 192.168.4.1)");
        ws2812_set_ap_mode();
    }

    /*
     * Bluepad32/BTstack runs independently of the Remote Play session task, following the exact
     * init sequence used in bluepad32's own src/main/main.c:
     *   btstack_init() -> uni_platform_set_custom() -> uni_init() ->
     *   btstack_run_loop_execute()
     *
     * btstack_run_loop_execute() NEVER RETURNS - it must be the last thing
     * app_main does. Wi-Fi, the web portal, and remoteplay_stub are already
     * running in their own tasks/handlers above this point, so they keep
     * working after control passes into the BTstack run loop.
     *
     * The controller callback feeds the live Chiaki feedback sender;
     * incoming PS5 LED feedback is forwarded to WS2812 GPIO48.
     */
    btstack_init();
    uni_platform_set_custom(bluepad32_bridge_get_platform());
    uni_init(0, NULL);
    btstack_run_loop_execute();
}
