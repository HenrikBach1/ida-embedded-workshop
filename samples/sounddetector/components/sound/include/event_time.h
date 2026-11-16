#pragma once
// event_time — turns the moment an event happened into the text shown for it. The board has no
// clock chip: real time exists only after an internet time sync, so events are stored as time
// since power-on and converted when they are drawn. Pure C (libc time only): no ESP-IDF,
// FreeRTOS or LVGL includes; host-tested in test/host/test_event_time.c.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EVENT_TIME_TEXT_MAX 16   // enough for "+9999:59:59" and "23:59:59"

// Write the time of an event that happened at `event_uptime_ms` into buf[cap].
//   boot_wall_s > 0 : the Unix time at which the device was switched on is known (clock time now
//                     minus uptime, worked out ONCE when the clock syncs, so that an event keeps
//                     the same text on every redraw). Output is the local clock time of the
//                     event, "HH:MM:SS" (local zone from the TZ environment variable).
//   boot_wall_s == 0: no clock. Output is the time since power-on, "+H:MM:SS".
// Returns false, with buf holding an empty string, if the text did not fit or cap is 0.
bool event_time_format(char *buf, size_t cap, uint64_t event_uptime_ms, int64_t boot_wall_s);
