#define _POSIX_C_SOURCE 200809L   // localtime_r under -std=c11 on the host
#include "event_time.h"
#include <stdio.h>
#include <time.h>

static bool finish(char *buf, size_t cap, int written)
{
    if (written >= 0 && (size_t)written < cap) return true;
    buf[0] = '\0';
    return false;
}

static bool format_uptime(char *buf, size_t cap, uint64_t event_uptime_ms)
{
    uint64_t s = event_uptime_ms / 1000u;
    return finish(buf, cap, snprintf(buf, cap, "+%u:%02u:%02u", (unsigned)(s / 3600u),
                                     (unsigned)(s / 60u % 60u), (unsigned)(s % 60u)));
}

bool event_time_format(char *buf, size_t cap, uint64_t event_uptime_ms, int64_t boot_wall_s)
{
    if (cap == 0) return false;
    if (boot_wall_s <= 0) return format_uptime(buf, cap, event_uptime_ms);

    time_t when = (time_t)(boot_wall_s + (int64_t)(event_uptime_ms / 1000u));
    struct tm tm;
    if (!localtime_r(&when, &tm)) { buf[0] = '\0'; return false; }
    return finish(buf, cap, snprintf(buf, cap, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec));
}
