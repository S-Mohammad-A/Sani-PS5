#pragma once
/*
 * Bridge between Bluepad32 (which reads the real DualSense controller over
 * Bluetooth) and the remoteplay module. Function/field names were taken
 * from the actual bluepad32 source (examples/esp32/main/my_platform.c and
 * uni_gamepad.h) that was uploaded - not from memory.
 *
 * app_main must:
 *   1) uni_platform_set_custom(bluepad32_bridge_get_platform());
 *   2) uni_init(0, NULL);
 *   3) btstack_run_loop_execute();  <-- this function never returns! It
 *      must be the last line of app_main, and everything else (Wi-Fi,
 *      portal, remoteplay) must already be running in its own tasks
 *      before this point.
 */
#include "uni.h"

struct uni_platform *bluepad32_bridge_get_platform(void);

/* Send PS5 rumble to the currently connected Bluetooth controller.
 * The actual Bluepad32 parser call is marshalled onto the BTstack run-loop
 * thread because Xbox rumble uses BTstack timers/state. */
void bluepad32_bridge_play_rumble(uint8_t weak_magnitude, uint8_t strong_magnitude);

/* Rumble enable/disable and status helpers */
void bluepad32_bridge_set_rumble_enabled(bool enabled);
bool bluepad32_bridge_get_rumble_enabled(void);
const char *bluepad32_bridge_get_controller_name(void);
int bluepad32_bridge_get_battery(void); /* 0-100, or -1 if unknown */
void bluepad32_bridge_reload_input_tuning(void);
