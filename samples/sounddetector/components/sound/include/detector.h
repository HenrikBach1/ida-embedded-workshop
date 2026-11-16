#pragma once
// detector — decides what counts as a "loud moment". Owns the learned room level (baseline), the
// sensitivity -> threshold rule, the merging of a long or stuttering noise into ONE event, and the
// 1..5 strength of an event. Pure C: no ESP-IDF, FreeRTOS or LVGL includes; host-tested in
// test/host/test_detector.c.
// Threading: one detector_t, fed by one task (the microphone task). No globals, no allocation.
// Levels are centi-dBFS as produced by mic_level_block(); times are milliseconds from any
// monotonic clock (differences are wrap-safe).
#include <stdbool.h>
#include <stdint.h>

#define DETECTOR_CAL_MS         5000u   // how long the room is listened to when learning
#define DETECTOR_QUIET_MS       1500u   // silence needed before an event is over
#define DETECTOR_SENS_MIN       1
#define DETECTOR_SENS_MAX       10
#define DETECTOR_SENS_DEFAULT   5
#define DETECTOR_STRENGTH_MAX   5

typedef enum {
    DET_NONE = 0,         // nothing to report for this block
    DET_CAL_DONE,         // learning finished; detector_baseline() is the new room level
    DET_EVENT_START,      // a loud moment began: count it once
    DET_EVENT_STRONGER,   // the running event got louder; detector_strength() went up
    DET_EVENT_END,        // quiet for DETECTOR_QUIET_MS; the next loud block is a new event
} detector_out_t;

// Zero-initialised = idle: no baseline, not learning, no event. Nothing is detected until a
// baseline exists (set from storage or learned). Treat the fields as private.
typedef struct {
    bool     has_baseline;
    int      baseline_cdb;
    bool     calibrating;
    bool     cal_started;      // false until the first block stamps cal_prev_ms
    uint32_t cal_prev_ms;      // time of the previous block while learning
    uint32_t cal_heard_ms;     // listening time so far; muted blocks do not count
    int64_t  cal_sum;
    int64_t  cal_sum_sq;
    uint32_t cal_n;
    bool     in_event;
    uint32_t last_loud_ms;
    int      strength;
} detector_t;

// Use a stored room level and stop any learning in progress.
void detector_set_baseline(detector_t *d, int baseline_cdb);

// Begin learning the room level from the next block on. What is learned is the top of the
// room's NORMAL range, not its average: mean + 3 standard deviations of the block levels. A
// quiet room flickers 5-8 dB above its own average (measured on the board), so a threshold
// measured from the average fires on plain background noise at the touchy end of the scale;
// a restless room gets a proportionally higher level. Ends any running event without
// reporting it; the old baseline stays valid for detector_baseline() until DET_CAL_DONE.
void detector_start_calibration(detector_t *d);

bool detector_is_calibrating(const detector_t *d);
bool detector_has_baseline(const detector_t *d);
int  detector_baseline(const detector_t *d);          // 0 while there is none

// Learning progress 0..100 (100 when not learning).
int  detector_cal_percent(const detector_t *d);

// How much louder than the room a sound must be, in centi-dB, for sensitivity 1 (deaf) to
// 10 (touchy). Out-of-range values are clamped.
int  detector_margin_cdb(int sensitivity);

// Strength 1..5 of the running or most recent event (0 before the first).
int  detector_strength(const detector_t *d);

// Feed one block. `muted` means the block must not be trusted (the device's own haptic click is
// audible to the microphone): it is skipped entirely, for learning and for detection. Learning
// needs DETECTOR_CAL_MS of blocks that were actually heard, however long that takes.
detector_out_t detector_feed(detector_t *d, int level_cdb, uint32_t now_ms, int sensitivity,
                             bool muted);
