#pragma once
// settings — the two things that survive a restart: the learned room level and the sensitivity.
// Owns the NVS namespace "sound" and one small writer task. The list of events is deliberately
// NOT stored anywhere.
// Depends on: nvs_flash (nvs_flash_init() must have run). Never includes LVGL, mic or app.
// Threading: the setters only note the new value and wake the writer task, so they are safe to
// call from an LVGL callback; the flash write happens about a second after the last change
// (a slider drag is one write, not thirty). settings_get() is safe from any task.
#include <stdbool.h>

typedef struct {
    bool has_baseline;      // false on a device that has never learned its room
    int  baseline_cdb;      // room level in centi-dBFS; meaningful only with has_baseline
    bool has_sensitivity;   // false until the user has moved the slider once
    int  sensitivity;       // as stored; the caller validates the range
} settings_t;

// Load from NVS and start the writer task. Call once, after nvs_flash_init(), before
// settings_get(). A missing or unreadable value just leaves its has_* flag false.
void settings_init(void);

// The values as loaded, updated by the setters. Before settings_init() this logs an error and
// returns all-false.
settings_t settings_get(void);

void settings_set_baseline(int baseline_cdb);
void settings_set_sensitivity(int sensitivity);
