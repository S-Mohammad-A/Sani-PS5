#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define CFG_MAX_STR   64
#define CFG_DEFAULT_AP_PASS  "12345678"
#define CFG_DEFAULT_GPIO_RGB 48
#define CFG_DEFAULT_GPIO_BTN 14

typedef struct {
    char wifi_ssid[CFG_MAX_STR];
    char wifi_pass[CFG_MAX_STR];

    char psn_account_id[CFG_MAX_STR];
    char regist_key[CFG_MAX_STR];
    char ps5_host[CFG_MAX_STR];

    uint8_t rp_regist_key[0x10];
    uint32_t rp_key_type;
    uint8_t rp_key[0x10];
    char server_nickname[0x20];
    bool registered;
    bool provisioned;

    bool rumble_enabled;
    uint8_t dz_global;
    uint8_t dz_lx, dz_ly, dz_rx, dz_ry;
    bool inv_lx, inv_ly, inv_rx, inv_ry;

    /* Hardware / AP (persisted) */
    uint8_t gpio_rgb;   /* WS2812 data pin, default 48 */
    uint8_t gpio_btn;   /* Connect button pin, default 14 */
    char ap_pass[CFG_MAX_STR]; /* SoftAP password, min 8 chars */
} app_config_t;

esp_err_t config_store_init(void);
esp_err_t config_store_load(app_config_t *out_cfg);
esp_err_t config_store_save(const app_config_t *cfg);

esp_err_t config_store_set_rumble(bool enabled);
esp_err_t config_store_set_host(const char *host);
esp_err_t config_store_set_input_tuning(const app_config_t *partial);

/* Reset AP password to default and save. */
esp_err_t config_store_reset_ap_pass(void);

/* Reset drift/mapping (input_cfg NVS) + AP password to defaults. */
esp_err_t config_store_reset_defaults(void);
