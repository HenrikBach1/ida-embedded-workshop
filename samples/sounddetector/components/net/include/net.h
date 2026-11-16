#pragma once
// net — Wi-Fi station. The sound detector uses the network for one thing only: the real time of
// day (net_time.h). Everything works without it. Depends on: esp_wifi, esp_netif, nvs_flash,
// wifi_secrets.h. Never includes LVGL or app.
// Threading: net_init() once from app_main; the queries are safe from any task.
#include <stdbool.h>
#include <stdint.h>

// ------------------------------------------------------------------ Wi-Fi --

// Start Wi-Fi STA with credentials from NVS (saved by net_wifi_join) or wifi_secrets.h, and keep
// reconnecting in the background. Returns immediately; poll net_is_connected().
void net_init(void);

// True once the station has an IP.
bool net_is_connected(void);

// Block until connected or `timeout_ms` elapses; true when connected.
bool net_wait_connected(uint32_t timeout_ms);

// Current AP signal strength in dBm (0 if not connected).
int net_rssi(void);

// Switch to another network: reconfigures the driver, reconnects, persists the credentials to NVS
// (namespace "wifi") for the next boot. The result is observable via net_is_connected() over the
// following seconds. Never logs the password.
bool net_wifi_join(const char *ssid, const char *pass);
