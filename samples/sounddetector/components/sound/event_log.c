#include "event_log.h"
#include <string.h>

void event_log_push(event_log_t *log, uint64_t uptime_ms, uint8_t strength)
{
    log->entry[log->next] = (event_entry_t){ .uptime_ms = uptime_ms, .strength = strength };
    log->next = (log->next + 1) % EVENT_LOG_CAP;
    if (log->stored < EVENT_LOG_CAP) log->stored++;
    log->total++;
}

static unsigned slot_of(const event_log_t *log, unsigned i)
{
    return (log->next + EVENT_LOG_CAP - 1 - i) % EVENT_LOG_CAP;
}

void event_log_set_newest_strength(event_log_t *log, uint8_t strength)
{
    if (log->stored == 0) return;
    log->entry[slot_of(log, 0)].strength = strength;
}

unsigned event_log_stored(const event_log_t *log) { return log->stored; }
unsigned event_log_total(const event_log_t *log)  { return log->total; }

const event_entry_t *event_log_get(const event_log_t *log, unsigned i)
{
    return i < log->stored ? &log->entry[slot_of(log, i)] : NULL;
}

void event_log_clear(event_log_t *log)
{
    memset(log, 0, sizeof(*log));
}
