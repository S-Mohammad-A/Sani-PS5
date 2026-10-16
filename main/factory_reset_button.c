#include "factory_reset_button.h"
#include "config_store.h"
#include "bluepad32_bridge.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "factory_rst";
static volatile bool s_reset_pending;

static void factory_reset_worker(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "Factory reset worker: restoring defaults…");
    if (config_store_reset_defaults() != ESP_OK) {
        ESP_LOGE(TAG, "config_store_reset_defaults failed");
        s_reset_pending = false;
        vTaskDelete(NULL);
        return;
    }
    bluepad32_bridge_reload_input_tuning();
    ESP_LOGW(TAG, "Defaults restored — rebooting");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    vTaskDelete(NULL);
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
            if ((xTaskGetTickCount() - changed_at) >= pdMS_TO_TICKS(40))
                stable = raw;
        } else {
            changed_at = xTaskGetTickCount();
        }

        if (last == 1 && stable == 0 && !s_reset_pending) {
            s_reset_pending = true;
            ESP_LOGW(TAG, "GPIO%d pressed — scheduling factory reset", FACTORY_RESET_GPIO);
            if (xTaskCreate(factory_reset_worker, "rst_work", 8192, NULL, 5, NULL) != pdPASS) {
                ESP_LOGE(TAG, "factory_reset_worker create failed");
                s_reset_pending = false;
            }
        }
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
