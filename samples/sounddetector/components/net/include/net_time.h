#pragma once
// net_time — real clock time from the internet (SNTP). The board has no clock chip, so the time
// is unknown until Wi-Fi is up and one sync has succeeded; without Wi-Fi it stays unknown
// forever and callers fall back to time since power-on.
// Depends on: esp_netif (net_init() must have run), lwIP SNTP. Never includes LVGL or app.
// Threading: both functions are safe from any task; net_time_now() never blocks.
#include <stdbool.h>
#include <stdint.h>

// Start SNTP against pool.ntp.org and set the local time zone (NET_TIME_TZ in net_time.c,
// Central European Time by default). Returns immediately; syncing happens in the background
// whenever the network is reachable.
void net_time_init(void);

// True once the clock has been set; *unix_s is then the current Unix time. False (and *unix_s
// untouched) while the time is still unknown.
bool net_time_now(int64_t *unix_s);
