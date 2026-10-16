#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define WS2812_GPIO      48
#define WS2812_LED_COUNT 1

esp_err_t ws2812_init(void);
esp_err_t ws2812_set_color(uint8_t r, uint8_t g, uint8_t b);

/* Deterministic status/state API. */
void ws2812_set_ap_mode(void);
void ws2812_set_wifi_status(bool connected, bool registered);
void ws2812_set_remote_connected(bool connected);
void ws2812_set_console_color(uint8_t r, uint8_t g, uint8_t b);
