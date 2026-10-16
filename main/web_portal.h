#pragma once
#include <stdbool.h>
#include "esp_err.h"

// Starts an HTTP server on port 80 that shows the settings form:
// Wi-Fi SSID/password, Regist Key, PSN Account ID (base64), PS5 console IP
esp_err_t web_portal_start(void);

// Stops the HTTP server to release lwIP sockets while Remote Play is active.
esp_err_t web_portal_stop(void);

// True only while the HTTP portal is running.
bool web_portal_is_running(void);

// Start the exact same Connect-to-PS5 operation used by POST /connect.
// Returns ESP_OK when a connection task was started.
esp_err_t web_portal_connect_from_button(void);
