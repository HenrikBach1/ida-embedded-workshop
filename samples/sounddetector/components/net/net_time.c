#include "net_time.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include <stdlib.h>
#include <time.h>

static const char *TAG = "net_time";

// POSIX TZ string for the device's location: CET/CEST with EU daylight-saving rules.
#ifndef NET_TIME_TZ
#define NET_TIME_TZ "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

// The clock starts at 1970 on every boot; anything after 2023-11-14 means SNTP has set it.
#define CLOCK_SET_AFTER_S 1700000000

void net_time_init(void)
{
    setenv("TZ", NET_TIME_TZ, 1);
    tzset();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "SNTP init: %s", esp_err_to_name(err)); return; }
    ESP_LOGI(TAG, "SNTP started (pool.ntp.org, TZ=%s)", NET_TIME_TZ);
}

bool net_time_now(int64_t *unix_s)
{
    time_t now = time(NULL);
    if (now < CLOCK_SET_AFTER_S) return false;
    *unix_s = (int64_t)now;
    return true;
}
