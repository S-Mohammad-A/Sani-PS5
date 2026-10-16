#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Xbox physical buttons (source side of remap UI) */
typedef enum {
    XBOX_BTN_A = 0,
    XBOX_BTN_B,
    XBOX_BTN_X,
    XBOX_BTN_Y,
    XBOX_BTN_LB,
    XBOX_BTN_RB,
    XBOX_BTN_LT,
    XBOX_BTN_RT,
    XBOX_BTN_LS,
    XBOX_BTN_RS,
    XBOX_BTN_VIEW,
    XBOX_BTN_MENU,
    XBOX_BTN_XBOX,
    XBOX_BTN_SHARE,
    XBOX_BTN_UP,
    XBOX_BTN_DOWN,
    XBOX_BTN_LEFT,
    XBOX_BTN_RIGHT,
    XBOX_BTN_COUNT
} xbox_btn_t;

/* PlayStation targets */
typedef enum {
    PS_BTN_NONE = 0,
    PS_BTN_CROSS,
    PS_BTN_CIRCLE,
    PS_BTN_SQUARE,
    PS_BTN_TRIANGLE,
    PS_BTN_L1,
    PS_BTN_R1,
    PS_BTN_L2,
    PS_BTN_R2,
    PS_BTN_L3,
    PS_BTN_R3,
    PS_BTN_SHARE,
    PS_BTN_OPTIONS,
    PS_BTN_PS,
    PS_BTN_TOUCHPAD,
    PS_BTN_UP,
    PS_BTN_DOWN,
    PS_BTN_LEFT,
    PS_BTN_RIGHT,
    PS_BTN_COUNT
} ps_btn_t;

typedef struct {
    uint8_t dz_global;
    uint8_t dz_lx, dz_ly, dz_rx, dz_ry;
    bool inv_lx, inv_ly, inv_rx, inv_ry;
    /* Triggers (Bluepad32 brake/throttle typically 0..1023) */
    uint8_t lt_dz, rt_dz;           /* deadzone percent 0..40 */
    uint16_t lt_idle, rt_idle;      /* raw at rest */
    uint16_t lt_max, rt_max;        /* raw at full press */
    uint8_t map[XBOX_BTN_COUNT];
} input_profile_t;

void input_config_set_defaults(input_profile_t *p);
esp_err_t input_config_load(input_profile_t *out);
esp_err_t input_config_save(const input_profile_t *p);

int16_t input_config_apply_axis(int16_t raw, uint8_t dz_pct, bool invert);
/* Map raw trigger (0..1023) -> 0..255 with idle/max/deadzone */
uint8_t input_config_apply_trigger(uint16_t raw, uint8_t dz_pct, uint16_t idle, uint16_t maxv);

uint32_t input_config_map_buttons(uint32_t xbox_buttons, uint8_t xbox_dpad, uint8_t xbox_misc,
                                  const input_profile_t *p);

const char *xbox_btn_name(xbox_btn_t b);
const char *ps_btn_name(ps_btn_t b);
