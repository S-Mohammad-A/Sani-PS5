#include "ws2812.h"
#include "config_store.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "ws2812";
static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_lock;
static bool s_remote_active = false;
static volatile bool s_blink_purple = false;
static TaskHandle_t s_blink_task;

static void set_pixel_locked(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;
    led_strip_set_pixel(s_strip, 0, r, g, b);
    (void)led_strip_refresh(s_strip);
}

static void blink_task(void *arg)
{
    (void)arg;
    bool on = false;
    while (1) {
        if (s_blink_purple && !s_remote_active) {
            on = !on;
            if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
                if (on) set_pixel_locked(48, 0, 48);
                else set_pixel_locked(0, 0, 0);
                xSemaphoreGive(s_lock);
            }
            vTaskDelay(pdMS_TO_TICKS(400));
        } else {
            on = false;
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

esp_err_t ws2812_init(void)
{
    app_config_t cfg;
    config_store_load(&cfg);
    int gpio = cfg.gpio_rgb ? cfg.gpio_rgb : WS2812_GPIO;

    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio,
        .max_leds = WS2812_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        return err;
    }
    s_lock = xSemaphoreCreateMutex();
    if(!s_lock) return ESP_ERR_NO_MEM;
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "WS2812 on GPIO%d", gpio);
    xTaskCreate(blink_task, "led_blink", 1536, NULL, 4, &s_blink_task);
    return ESP_OK;
}

esp_err_t ws2812_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    if(s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE){
        set_pixel_locked(r,g,b);
        xSemaphoreGive(s_lock);
    } else {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void ws2812_set_ap_mode(void)
{
    s_blink_purple = false;
    (void)ws2812_set_color(48, 32, 0);
}

void ws2812_set_wifi_status(bool connected, bool registered)
{
    if(s_remote_active) return;
    s_blink_purple = false;
    if(!connected){
        ws2812_set_ap_mode();
        return;
    }
    if(registered) (void)ws2812_set_color(0,48,0);
    else (void)ws2812_set_color(48,0,0);
}

void ws2812_set_remote_connected(bool connected)
{
    s_remote_active = connected;
    if(connected) {
        s_blink_purple = false;
        (void)ws2812_set_color(0,0,48);
    } else {
        s_blink_purple = true;
        (void)ws2812_set_color(48,0,48);
    }
}

void ws2812_set_console_color(uint8_t r, uint8_t g, uint8_t b)
{
    if(!s_remote_active) return;
    s_blink_purple = false;
    (void)ws2812_set_color(r,g,b);
}
