#pragma once
// app_model — the read-only picture of the app that the screens draw from (private to app).
// Built by app.c on the LVGL task for one render call; the pointers are valid only during that
// call. Screens never change it and never look anywhere else for data.
#include <stdbool.h>
#include <stdint.h>
#include "event_log.h"

typedef struct {
    const event_log_t *log;   // loud moments, newest first
    uint32_t accent;          // 0xRRGGBB ring colour for the current count
    int      sensitivity;     // 1..10
    bool     mic_ok;          // false: the microphone did not start
    bool     learning;        // the room level is being learned right now
    int      learn_pct;       // 0..100 while learning
    bool     has_baseline;
    int      baseline_cdb;    // learned room level, centi-dBFS
    int64_t  boot_wall_s;     // Unix time of power-on, 0 while the clock is unknown
} app_model_t;
