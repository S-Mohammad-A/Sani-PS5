#include "connect_button.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "web_portal.h"
#include "config_store.h"

#define CONNECT_BUTTON_POLL_MS 20
#define CONNECT_BUTTON_DEBOUNCE_MS 30

static const char *TAG = "connect_button";
static int s_btn_gpio = CFG_DEFAULT_GPIO_BTN;

static void connect_button_task(void *arg)
{
    (void)arg;
    int last = 1;
    int stable = 1;
    TickType_t changed_at = xTaskGetTickCount();

    for (;;) {
        int raw = gpio_get_level(s_btn_gpio);
        if (raw != stable) {
            if ((xTaskGetTickCount() - changed_at) >= pdMS_TO_TICKS(CONNECT_BUTTON_DEBOUNCE_MS)) {
                stable = raw;
            }
        } else {
            changed_at = xTaskGetTickCount();
        }

        /* Falling edge (active-low): Connect while portal is up */
        if (last == 1 && stable == 0) {
            if (web_portal_is_running())
                (void)web_portal_connect_from_button();
        }
        last = stable;
        vTaskDelay(pdMS_TO_TICKS(CONNECT_BUTTON_POLL_MS));
    }
}

esp_err_t connect_button_init(void)
{
    app_config_t cfg;
    config_store_load(&cfg);
    s_btn_gpio = cfg.gpio_btn ? cfg.gpio_btn : CFG_DEFAULT_GPIO_BTN;

    const gpio_config_t gcfg = {
        .pin_bit_mask = 1ULL << s_btn_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&gcfg);
    if (err != ESP_OK)
        return err;

    if (xTaskCreate(connect_button_task, "connect_btn", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Connect button task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Connect button GPIO%d (short press while portal up = Connect)", s_btn_gpio);
    return ESP_OK;
}
