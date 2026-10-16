#include "input_config.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include "controller/uni_gamepad.h"

static const char *TAG = "input_cfg";
static const char *NS = "input_cfg";

void input_config_set_defaults(input_profile_t *p)
{
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->dz_global = 4;
    p->dz_lx = p->dz_ly = p->dz_rx = p->dz_ry = 4;
    p->inv_lx = p->inv_ly = p->inv_rx = p->inv_ry = false;
    /* Match Gamepad.drift defaults */
    p->lt_dz = 1;
    p->rt_dz = 1;
    p->lt_idle = 14;
    p->rt_idle = 14;
    p->lt_max = 1023;
    p->rt_max = 1023;

    p->map[XBOX_BTN_A]     = PS_BTN_CROSS;
    p->map[XBOX_BTN_B]     = PS_BTN_CIRCLE;
    p->map[XBOX_BTN_X]     = PS_BTN_SQUARE;
    p->map[XBOX_BTN_Y]     = PS_BTN_TRIANGLE;
    p->map[XBOX_BTN_LB]    = PS_BTN_L1;
    p->map[XBOX_BTN_RB]    = PS_BTN_R1;
    p->map[XBOX_BTN_LT]    = PS_BTN_L2;
    p->map[XBOX_BTN_RT]    = PS_BTN_R2;
    p->map[XBOX_BTN_LS]    = PS_BTN_L3;
    p->map[XBOX_BTN_RS]    = PS_BTN_R3;
    p->map[XBOX_BTN_VIEW]  = PS_BTN_SHARE;
    p->map[XBOX_BTN_MENU]  = PS_BTN_OPTIONS;
    p->map[XBOX_BTN_XBOX]  = PS_BTN_PS;
    p->map[XBOX_BTN_SHARE] = PS_BTN_TOUCHPAD;
    p->map[XBOX_BTN_UP]    = PS_BTN_UP;
    p->map[XBOX_BTN_DOWN]  = PS_BTN_DOWN;
    p->map[XBOX_BTN_LEFT]  = PS_BTN_LEFT;
    p->map[XBOX_BTN_RIGHT] = PS_BTN_RIGHT;
}

esp_err_t input_config_load(input_profile_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    input_config_set_defaults(out);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    uint8_t u8 = 0;
    uint16_t u16 = 0;
    if (nvs_get_u8(h, "dz_g", &u8) == ESP_OK) out->dz_global = u8 > 40 ? 40 : u8;
    if (nvs_get_u8(h, "dz_lx", &u8) == ESP_OK) out->dz_lx = u8 > 40 ? 40 : u8;
    if (nvs_get_u8(h, "dz_ly", &u8) == ESP_OK) out->dz_ly = u8 > 40 ? 40 : u8;
    if (nvs_get_u8(h, "dz_rx", &u8) == ESP_OK) out->dz_rx = u8 > 40 ? 40 : u8;
    if (nvs_get_u8(h, "dz_ry", &u8) == ESP_OK) out->dz_ry = u8 > 40 ? 40 : u8;
    if (nvs_get_u8(h, "inv_lx", &u8) == ESP_OK) out->inv_lx = u8 != 0;
    if (nvs_get_u8(h, "inv_ly", &u8) == ESP_OK) out->inv_ly = u8 != 0;
    if (nvs_get_u8(h, "inv_rx", &u8) == ESP_OK) out->inv_rx = u8 != 0;
    if (nvs_get_u8(h, "inv_ry", &u8) == ESP_OK) out->inv_ry = u8 != 0;
    if (nvs_get_u8(h, "lt_dz", &u8) == ESP_OK) out->lt_dz = u8 > 40 ? 40 : u8;
    if (nvs_get_u8(h, "rt_dz", &u8) == ESP_OK) out->rt_dz = u8 > 40 ? 40 : u8;
    if (nvs_get_u16(h, "lt_idle", &u16) == ESP_OK) out->lt_idle = u16;
    if (nvs_get_u16(h, "rt_idle", &u16) == ESP_OK) out->rt_idle = u16;
    if (nvs_get_u16(h, "lt_max", &u16) == ESP_OK) out->lt_max = u16 ? u16 : 1023;
    if (nvs_get_u16(h, "rt_max", &u16) == ESP_OK) out->rt_max = u16 ? u16 : 1023;

    size_t len = sizeof(out->map);
    err = nvs_get_blob(h, "btn_map", out->map, &len);
    if (err != ESP_OK || len != sizeof(out->map)) {
        input_profile_t d;
        input_config_set_defaults(&d);
        memcpy(out->map, d.map, sizeof(out->map));
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t input_config_save(const input_profile_t *p)
{
    if (!p) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

#define SET(c) do { err = (c); if (err != ESP_OK) goto out; } while (0)
    SET(nvs_set_u8(h, "dz_g", p->dz_global > 40 ? 40 : p->dz_global));
    SET(nvs_set_u8(h, "dz_lx", p->dz_lx > 40 ? 40 : p->dz_lx));
    SET(nvs_set_u8(h, "dz_ly", p->dz_ly > 40 ? 40 : p->dz_ly));
    SET(nvs_set_u8(h, "dz_rx", p->dz_rx > 40 ? 40 : p->dz_rx));
    SET(nvs_set_u8(h, "dz_ry", p->dz_ry > 40 ? 40 : p->dz_ry));
    SET(nvs_set_u8(h, "inv_lx", p->inv_lx ? 1 : 0));
    SET(nvs_set_u8(h, "inv_ly", p->inv_ly ? 1 : 0));
    SET(nvs_set_u8(h, "inv_rx", p->inv_rx ? 1 : 0));
    SET(nvs_set_u8(h, "inv_ry", p->inv_ry ? 1 : 0));
    SET(nvs_set_u8(h, "lt_dz", p->lt_dz > 40 ? 40 : p->lt_dz));
    SET(nvs_set_u8(h, "rt_dz", p->rt_dz > 40 ? 40 : p->rt_dz));
    SET(nvs_set_u16(h, "lt_idle", p->lt_idle));
    SET(nvs_set_u16(h, "rt_idle", p->rt_idle));
    SET(nvs_set_u16(h, "lt_max", p->lt_max ? p->lt_max : 1023));
    SET(nvs_set_u16(h, "rt_max", p->rt_max ? p->rt_max : 1023));
    SET(nvs_set_blob(h, "btn_map", p->map, sizeof(p->map)));
    err = nvs_commit(h);
out:
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    return err;
#undef SET
}

int16_t input_config_apply_axis(int16_t raw, uint8_t dz_pct, bool invert)
{
    if (invert) raw = (int16_t)(-raw);
    int absv = raw < 0 ? -raw : raw;
    int threshold = (512 * (dz_pct > 40 ? 40 : dz_pct)) / 100;
    if (absv <= threshold) return 0;
    int sign = raw < 0 ? -1 : 1;
    int span = 512 - threshold;
    if (span < 1) span = 1;
    int out = ((absv - threshold) * 512) / span;
    if (out > 511) out = 511;
    return (int16_t)(sign * out);
}

uint8_t input_config_apply_trigger(uint16_t raw, uint8_t dz_pct, uint16_t idle, uint16_t maxv)
{
    if (maxv <= idle) maxv = idle + 1;
    if (raw <= idle) return 0;
    if (raw >= maxv) return 255;
    /* Linear map idle..max -> 0..255 */
    uint32_t span = (uint32_t)(maxv - idle);
    uint32_t v = (uint32_t)(raw - idle);
    uint32_t out = (v * 255u) / span;
    if (out > 255) out = 255;
    /* Percent deadzone on the 0..255 result */
    uint32_t thr = (255u * (dz_pct > 40 ? 40 : dz_pct)) / 100u;
    if (out <= thr) return 0;
    uint32_t rem = 255u - thr;
    if (rem < 1) rem = 1;
    out = ((out - thr) * 255u) / rem;
    if (out > 255) out = 255;
    return (uint8_t)out;
}

static uint32_t ps_to_chiaki(ps_btn_t t)
{
    switch (t) {
    case PS_BTN_CROSS:    return (1u << 0);
    case PS_BTN_CIRCLE:   return (1u << 1);
    case PS_BTN_SQUARE:   return (1u << 2);
    case PS_BTN_TRIANGLE: return (1u << 3);
    case PS_BTN_LEFT:     return (1u << 4);
    case PS_BTN_RIGHT:    return (1u << 5);
    case PS_BTN_UP:       return (1u << 6);
    case PS_BTN_DOWN:     return (1u << 7);
    case PS_BTN_L1:       return (1u << 8);
    case PS_BTN_R1:       return (1u << 9);
    case PS_BTN_L3:       return (1u << 10);
    case PS_BTN_R3:       return (1u << 11);
    case PS_BTN_OPTIONS:  return (1u << 12);
    case PS_BTN_SHARE:    return (1u << 13);
    case PS_BTN_TOUCHPAD: return (1u << 14);
    case PS_BTN_PS:       return (1u << 15);
    case PS_BTN_L2:       return (1u << 16);
    case PS_BTN_R2:       return (1u << 17);
    default: return 0;
    }
}

uint32_t input_config_map_buttons(uint32_t xbox_buttons, uint8_t xbox_dpad, uint8_t xbox_misc,
                                  const input_profile_t *p)
{
    uint32_t out = 0;
    if (!p) return 0;

    static const struct { xbox_btn_t xb; uint32_t mask; int kind; } src[] = {
        { XBOX_BTN_A,     BUTTON_A, 0 },
        { XBOX_BTN_B,     BUTTON_B, 0 },
        { XBOX_BTN_X,     BUTTON_X, 0 },
        { XBOX_BTN_Y,     BUTTON_Y, 0 },
        { XBOX_BTN_LB,    BUTTON_SHOULDER_L, 0 },
        { XBOX_BTN_RB,    BUTTON_SHOULDER_R, 0 },
        { XBOX_BTN_LT,    BUTTON_TRIGGER_L, 0 },
        { XBOX_BTN_RT,    BUTTON_TRIGGER_R, 0 },
        { XBOX_BTN_LS,    BUTTON_THUMB_L, 0 },
        { XBOX_BTN_RS,    BUTTON_THUMB_R, 0 },
        { XBOX_BTN_VIEW,  MISC_BUTTON_SELECT, 1 },
        { XBOX_BTN_MENU,  MISC_BUTTON_START, 1 },
        { XBOX_BTN_XBOX,  MISC_BUTTON_SYSTEM, 1 },
        { XBOX_BTN_SHARE, MISC_BUTTON_CAPTURE, 1 },
        { XBOX_BTN_UP,    DPAD_UP, 2 },
        { XBOX_BTN_DOWN,  DPAD_DOWN, 2 },
        { XBOX_BTN_LEFT,  DPAD_LEFT, 2 },
        { XBOX_BTN_RIGHT, DPAD_RIGHT, 2 },
    };

    for (unsigned i = 0; i < sizeof(src)/sizeof(src[0]); ++i) {
        bool pressed = false;
        if (src[i].kind == 0) pressed = (xbox_buttons & src[i].mask) != 0;
        else if (src[i].kind == 1) pressed = (xbox_misc & src[i].mask) != 0;
        else pressed = (xbox_dpad & src[i].mask) != 0;
        if (!pressed) continue;
        uint8_t target = p->map[src[i].xb];
        if (target == PS_BTN_NONE || target >= PS_BTN_COUNT) continue;
        out |= ps_to_chiaki((ps_btn_t)target);
    }
    return out;
}

const char *xbox_btn_name(xbox_btn_t b)
{
    static const char *n[XBOX_BTN_COUNT] = {
        "A", "B", "X", "Y", "LB", "RB", "LT", "RT",
        "LS", "RS", "View", "Menu", "Xbox", "Share",
        "D-Up", "D-Down", "D-Left", "D-Right"
    };
    return (b < XBOX_BTN_COUNT) ? n[b] : "?";
}

const char *ps_btn_name(ps_btn_t b)
{
    static const char *n[PS_BTN_COUNT] = {
        "None", "Cross", "Circle", "Square", "Triangle",
        "L1", "R1", "L2", "R2", "L3", "R3",
        "Share", "Options", "PS", "Touchpad",
        "D-Up", "D-Down", "D-Left", "D-Right"
    };
    return (b < PS_BTN_COUNT) ? n[b] : "?";
}
