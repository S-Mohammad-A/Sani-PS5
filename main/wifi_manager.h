#pragma once
#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_manager_start(void);
bool wifi_manager_is_connected(void);
/* WIFI_PS_NONE while Remote Play is active; restore MIN_MODEM when idle. */
esp_err_t wifi_manager_set_performance_mode(bool enable);
