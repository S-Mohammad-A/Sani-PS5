#pragma once

#include "esp_err.h"

// Initializes the physical Connect button.
// Default wiring: GPIO14 -> momentary button -> GND; internal pull-up is used.
esp_err_t connect_button_init(void);
