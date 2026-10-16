#pragma once
#include "esp_err.h"

/* GPIO13 active-low: factory reset (drift, mapping, AP pass, default GPIOs) + reboot */
#define FACTORY_RESET_GPIO 13

esp_err_t factory_reset_button_init(void);
