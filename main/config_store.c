#include <string.h>
#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "input_config.h"

static const char *TAG = "config_store";
static const char *NS  = "app_cfg";

esp_err_t config_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t get_str(nvs_handle_t h, const char *key, char *out, size_t out_len)
{
    size_t len = out_len;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        out[0] = '\0';
        return ESP_OK;
    }
    if (err != ESP_OK)
        out[0] = '\0';
    return err;
}

static esp_err_t get_blob_exact(nvs_handle_t h, const char *key, void *out, size_t expected)
{
    size_t len = expected;
    esp_err_t err = nvs_get_blob(h, key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(out, 0, expected);
        return ESP_OK;
    }
    if (err != ESP_OK || len != expected) {
        memset(out, 0, expected);
        return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
    }
    return ESP_OK;
}

static uint8_t clamp_dz(uint8_t v)
{
    return v > 40 ? 40 : v;
}

esp_err_t config_store_load(app_config_t *out_cfg)
{
    if (!out_cfg)
        return ESP_ERR_INVALID_ARG;

    memset(out_cfg, 0, sizeof(*out_cfg));
    /* Defaults */
    out_cfg->rumble_enabled = true;
    out_cfg->dz_global = 4;
    out_cfg->dz_lx = out_cfg->dz_ly = out_cfg->dz_rx = out_cfg->dz_ry = 4;
    out_cfg->gpio_rgb = CFG_DEFAULT_GPIO_RGB;
    out_cfg->gpio_btn = CFG_DEFAULT_GPIO_BTN;
    strncpy(out_cfg->ap_pass, CFG_DEFAULT_AP_PASS, sizeof(out_cfg->ap_pass) - 1);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (err != ESP_OK)
        return err;

    esp_err_t first_err = ESP_OK;

#define GET_STR(k, f) do { \
        esp_err_t e = get_str(h, k, f, sizeof(f)); \
        if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND && first_err == ESP_OK) first_err = e; \
    } while (0)
#define GET_BLOB(k, f) do { \
        esp_err_t e = get_blob_exact(h, k, f, sizeof(f)); \
        if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND && first_err == ESP_OK) first_err = e; \
    } while (0)

    GET_STR("wifi_ssid", out_cfg->wifi_ssid);
    GET_STR("wifi_pass", out_cfg->wifi_pass);
    GET_STR("psn_id", out_cfg->psn_account_id);
    GET_STR("regist", out_cfg->regist_key);
    GET_STR("ps5_host", out_cfg->ps5_host);
    GET_STR("server_nick", out_cfg->server_nickname);
    GET_BLOB("rp_regist", out_cfg->rp_regist_key);
    GET_BLOB("rp_key", out_cfg->rp_key);

    esp_err_t e = nvs_get_u32(h, "rp_key_type", &out_cfg->rp_key_type);
    if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND && first_err == ESP_OK)
        first_err = e;

    uint8_t u8 = 0;
    if (nvs_get_u8(h, "registered", &u8) == ESP_OK) out_cfg->registered = (u8 != 0);
    if (nvs_get_u8(h, "provisioned", &u8) == ESP_OK) out_cfg->provisioned = (u8 != 0);
    if (nvs_get_u8(h, "rumble_en", &u8) == ESP_OK) out_cfg->rumble_enabled = (u8 != 0);
    if (nvs_get_u8(h, "dz_g", &u8) == ESP_OK) out_cfg->dz_global = clamp_dz(u8);
    if (nvs_get_u8(h, "dz_lx", &u8) == ESP_OK) out_cfg->dz_lx = clamp_dz(u8);
    if (nvs_get_u8(h, "dz_ly", &u8) == ESP_OK) out_cfg->dz_ly = clamp_dz(u8);
    if (nvs_get_u8(h, "dz_rx", &u8) == ESP_OK) out_cfg->dz_rx = clamp_dz(u8);
    if (nvs_get_u8(h, "dz_ry", &u8) == ESP_OK) out_cfg->dz_ry = clamp_dz(u8);
    if (nvs_get_u8(h, "inv_lx", &u8) == ESP_OK) out_cfg->inv_lx = (u8 != 0);
    if (nvs_get_u8(h, "inv_ly", &u8) == ESP_OK) out_cfg->inv_ly = (u8 != 0);
    if (nvs_get_u8(h, "inv_rx", &u8) == ESP_OK) out_cfg->inv_rx = (u8 != 0);
    if (nvs_get_u8(h, "inv_ry", &u8) == ESP_OK) out_cfg->inv_ry = (u8 != 0);
    if (nvs_get_u8(h, "gpio_rgb", &u8) == ESP_OK && u8 > 0) out_cfg->gpio_rgb = u8;
    if (nvs_get_u8(h, "gpio_btn", &u8) == ESP_OK && u8 > 0) out_cfg->gpio_btn = u8;
    {
        char ap[CFG_MAX_STR] = {0};
        if (get_str(h, "ap_pass", ap, sizeof(ap)) == ESP_OK && strlen(ap) >= 8)
            strncpy(out_cfg->ap_pass, ap, sizeof(out_cfg->ap_pass) - 1);
    }

#undef GET_STR
#undef GET_BLOB
    nvs_close(h);
    return first_err;
}

esp_err_t config_store_save(const app_config_t *cfg)
{
    if (!cfg)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

#define SET_CALL(_call) do { \
        err = (_call); \
        if (err != ESP_OK) goto out; \
    } while (0)

    SET_CALL(nvs_set_str(h, "wifi_ssid", cfg->wifi_ssid));
    SET_CALL(nvs_set_str(h, "wifi_pass", cfg->wifi_pass));
    SET_CALL(nvs_set_str(h, "psn_id", cfg->psn_account_id));
    SET_CALL(nvs_set_str(h, "regist", cfg->regist_key));
    SET_CALL(nvs_set_str(h, "ps5_host", cfg->ps5_host));
    SET_CALL(nvs_set_str(h, "server_nick", cfg->server_nickname));
    SET_CALL(nvs_set_blob(h, "rp_regist", cfg->rp_regist_key, sizeof(cfg->rp_regist_key)));
    SET_CALL(nvs_set_u32(h, "rp_key_type", cfg->rp_key_type));
    SET_CALL(nvs_set_blob(h, "rp_key", cfg->rp_key, sizeof(cfg->rp_key)));
    SET_CALL(nvs_set_u8(h, "registered", cfg->registered ? 1 : 0));
    SET_CALL(nvs_set_u8(h, "provisioned", cfg->provisioned ? 1 : 0));
    SET_CALL(nvs_set_u8(h, "rumble_en", cfg->rumble_enabled ? 1 : 0));
    SET_CALL(nvs_set_u8(h, "dz_g", clamp_dz(cfg->dz_global)));
    SET_CALL(nvs_set_u8(h, "dz_lx", clamp_dz(cfg->dz_lx)));
    SET_CALL(nvs_set_u8(h, "dz_ly", clamp_dz(cfg->dz_ly)));
    SET_CALL(nvs_set_u8(h, "dz_rx", clamp_dz(cfg->dz_rx)));
    SET_CALL(nvs_set_u8(h, "dz_ry", clamp_dz(cfg->dz_ry)));
    SET_CALL(nvs_set_u8(h, "inv_lx", cfg->inv_lx ? 1 : 0));
    SET_CALL(nvs_set_u8(h, "inv_ly", cfg->inv_ly ? 1 : 0));
    SET_CALL(nvs_set_u8(h, "inv_rx", cfg->inv_rx ? 1 : 0));
    SET_CALL(nvs_set_u8(h, "inv_ry", cfg->inv_ry ? 1 : 0));
    SET_CALL(nvs_set_u8(h, "gpio_rgb", cfg->gpio_rgb ? cfg->gpio_rgb : CFG_DEFAULT_GPIO_RGB));
    SET_CALL(nvs_set_u8(h, "gpio_btn", cfg->gpio_btn ? cfg->gpio_btn : CFG_DEFAULT_GPIO_BTN));
    {
        const char *ap = (cfg->ap_pass[0] && strlen(cfg->ap_pass) >= 8) ? cfg->ap_pass : CFG_DEFAULT_AP_PASS;
        SET_CALL(nvs_set_str(h, "ap_pass", ap));
    }

    err = nvs_commit(h);

out:
    if (err != ESP_OK)
        ESP_LOGE(TAG, "NVS save failed: %s", esp_err_to_name(err));
    nvs_close(h);
#undef SET_CALL
    return err;
}

esp_err_t config_store_set_rumble(bool enabled)
{
    app_config_t cfg;
    esp_err_t e = config_store_load(&cfg);
    if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND)
        return e;
    cfg.rumble_enabled = enabled;
    return config_store_save(&cfg);
}

esp_err_t config_store_set_host(const char *host)
{
    if (!host) return ESP_ERR_INVALID_ARG;
    app_config_t cfg;
    esp_err_t e = config_store_load(&cfg);
    if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND)
        return e;
    strncpy(cfg.ps5_host, host, sizeof(cfg.ps5_host) - 1);
    cfg.ps5_host[sizeof(cfg.ps5_host) - 1] = '\0';
    return config_store_save(&cfg);
}

esp_err_t config_store_set_input_tuning(const app_config_t *partial)
{
    if (!partial) return ESP_ERR_INVALID_ARG;
    app_config_t cfg;
    esp_err_t e = config_store_load(&cfg);
    if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND)
        return e;
    cfg.dz_global = partial->dz_global;
    cfg.dz_lx = partial->dz_lx;
    cfg.dz_ly = partial->dz_ly;
    cfg.dz_rx = partial->dz_rx;
    cfg.dz_ry = partial->dz_ry;
    cfg.inv_lx = partial->inv_lx;
    cfg.inv_ly = partial->inv_ly;
    cfg.inv_rx = partial->inv_rx;
    cfg.inv_ry = partial->inv_ry;
    return config_store_save(&cfg);
}


esp_err_t config_store_reset_ap_pass(void)
{
    app_config_t cfg;
    esp_err_t e = config_store_load(&cfg);
    if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND)
        return e;
    strncpy(cfg.ap_pass, CFG_DEFAULT_AP_PASS, sizeof(cfg.ap_pass) - 1);
    cfg.ap_pass[sizeof(cfg.ap_pass) - 1] = '\0';
    e = config_store_save(&cfg);
    if (e == ESP_OK)
        ESP_LOGW(TAG, "AP password reset to default");
    return e;
}

esp_err_t config_store_reset_defaults(void)
{
    app_config_t cfg;
    esp_err_t e = config_store_load(&cfg);
    if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND)
        return e;

    cfg.dz_global = 4;
    cfg.dz_lx = cfg.dz_ly = cfg.dz_rx = cfg.dz_ry = 4;
    cfg.inv_lx = cfg.inv_ly = cfg.inv_rx = cfg.inv_ry = false;
    strncpy(cfg.ap_pass, CFG_DEFAULT_AP_PASS, sizeof(cfg.ap_pass) - 1);
    cfg.ap_pass[sizeof(cfg.ap_pass) - 1] = '\0';
    cfg.gpio_rgb = CFG_DEFAULT_GPIO_RGB;
    cfg.gpio_btn = CFG_DEFAULT_GPIO_BTN;

    e = config_store_save(&cfg);
    if (e != ESP_OK)
        return e;

    input_profile_t prof;
    input_config_set_defaults(&prof);
    e = input_config_save(&prof);
    if (e == ESP_OK)
        ESP_LOGW(TAG, "Defaults restored: drift, mapping, AP pass, GPIO");
    return e;
}
