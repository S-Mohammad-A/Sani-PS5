#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include <chiaki/regist.h>

typedef struct {
    uint16_t buttons;
    uint8_t dpad;
    uint8_t misc_buttons;
    int16_t left_x, left_y, right_x, right_y;
    uint8_t l2, r2;
} rp_controller_state_t;

typedef void (*rp_lightbar_cb_t)(uint8_t r, uint8_t g, uint8_t b);

esp_err_t remoteplay_stub_init(rp_lightbar_cb_t on_lightbar);
esp_err_t remoteplay_stub_register(const char *host, const char *psn_account_id_b64,
                                   const char *pin, ChiakiRegisteredHost *out_host);
esp_err_t remoteplay_stub_connect_registered(const char *host,
                                              const uint8_t *rp_regist_key,
                                              const uint8_t *rp_key);
void remoteplay_stub_send_controller_state(const rp_controller_state_t *state);
