#pragma once
// event_log — remembers the most recent loud moments, newest first, and how many there have been
// since the last clear. RAM only by design: the list does not survive a restart. Pure C: no
// ESP-IDF, FreeRTOS or LVGL includes; host-tested in test/host/test_event_log.c.
// Threading: not thread-safe; the app keeps its one log on the LVGL task.
#include <stdint.h>

#define EVENT_LOG_CAP 20

typedef struct {
    uint64_t uptime_ms;   // when it started, in milliseconds since power-on
    uint8_t  strength;    // 1..5
} event_entry_t;

// Zero-initialised = empty. Treat the fields as private.
typedef struct {
    event_entry_t entry[EVENT_LOG_CAP];
    unsigned      next;      // slot the next push writes
    unsigned      stored;    // 0..EVENT_LOG_CAP
    unsigned      total;     // pushes since the last clear; keeps counting past the cap
} event_log_t;

// Record a new loud moment. When full, the oldest entry is overwritten.
void event_log_push(event_log_t *log, uint64_t uptime_ms, uint8_t strength);

// Raise the strength of the newest entry (an event that got louder). No-op on an empty log.
void event_log_set_newest_strength(event_log_t *log, uint8_t strength);

unsigned event_log_stored(const event_log_t *log);   // entries available to event_log_get()
unsigned event_log_total(const event_log_t *log);    // loud moments since the last clear

// Entry `i`, 0 = newest. NULL when i >= event_log_stored(). The pointer is valid until the
// next push or clear.
const event_entry_t *event_log_get(const event_log_t *log, unsigned i);

void event_log_clear(event_log_t *log);
