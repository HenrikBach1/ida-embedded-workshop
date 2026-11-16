#pragma once
// app_monitor — the listening half of the app (private to app): runs the detector on the
// microphone task and hands the results to the LVGL task.
// Threading: monitor_start() once. Everything else is safe from any task and never blocks,
// except monitor_selftest(), which sleeps. Nothing here touches LVGL.
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MONITOR_LOUD_START,      // a new loud moment: count it
    MONITOR_LOUD_STRONGER,   // the current one got louder
} monitor_kind_t;

typedef struct {
    monitor_kind_t kind;
    int            strength;     // 1..5 for the two LOUD kinds
    uint64_t       uptime_ms;    // when it happened, milliseconds since power-on
} monitor_msg_t;

// Start the microphone and the detector. Without a stored baseline, learning starts at once.
// Returns false (logged) if the microphone could not be started.
bool monitor_start(bool has_baseline, int baseline_cdb, int sensitivity);

// Next pending message, if any. Call from the LVGL timer until it returns false.
bool monitor_poll(monitor_msg_t *out);

void monitor_set_sensitivity(int sensitivity);
void monitor_request_relearn(void);

// Ignore the microphone for `ms` from now: it hears the haptic motor about 28 dB above a quiet
// room, so every click must be followed by this or the detector triggers itself.
void monitor_mute_ms(uint32_t ms);

// Stop (or resume) listening altogether; used by the test console so that a scripted walk is
// not disturbed by real sounds in the room. Learning is paused too: it needs 5 s of blocks
// that were actually heard.
void monitor_set_paused(bool paused);
bool monitor_is_paused(void);

bool monitor_is_learning(void);
int  monitor_learn_percent(void);
bool monitor_has_baseline(void);
int  monitor_baseline(void);

// Latest block level, room level and current threshold, all centi-dBFS.
void monitor_levels(int *level_cdb, int *baseline_cdb, int *threshold_cdb);

// Loudest block since the previous call (centi-dBFS). A single "latest level" misses a clap
// that falls between two reads; this does not. One reader only (the test console).
int monitor_take_peak(void);

// Measure the loudest block over a quiet 400 ms, call `stimulus`, measure the loudest block over
// the next 500 ms. Detection is muted throughout. Sleeps about a second.
void monitor_selftest(void (*stimulus)(void), int *quiet_cdb, int *stimulus_cdb);
