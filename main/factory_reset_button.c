#include "factory_reset_button.h"
#include "config_store.h"
#include "bluepad32_bridge.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "factory_rst";

static void do_factory_reset_and_reboot(void)
{
    ESP_LOGW(TAG, "GPIO%d pressed — factory reset (drift/map/AP/GPIO) + reboot", FACTORY_RESET_GPIO);
    if (config_store_reset_defaults() != ESP_OK) {
        ESP_LOGE(TAG, "config_store_reset_defaults failed");
        return;
    }
    bluepad32_bridge_reload_input_tuning();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

static void factory_reset_task(void *arg)
{
    (void)arg;
    int last = 1;
    int stable = 1;
    TickType_t changed_at = xTaskGetTickCount();

    for (;;) {
        int raw = gpio_get_level(FACTORY_RESET_GPIO);
        if (raw != stable) {
            if ((xTaskGetTickCount() - changed_at) >= pdMS_TO_TICKS(30))
                stable = raw;
        } else {
            changed_at = xTaskGetTickCount();
        }

        if (last == 1 && stable == 0)
            do_factory_reset_and_reboot();

        last = stable;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t factory_reset_button_init(void)
{
    const gpio_config_t gcfg = {
        .pin_bit_mask = 1ULL << FACTORY_RESET_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&gcfg);
    if (err != ESP_OK)
        return err;

    if (xTaskCreate(factory_reset_task, "factory_rst", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Factory-reset button on GPIO%d (press = defaults + reboot)", FACTORY_RESET_GPIO);
    return ESP_OK;
}
