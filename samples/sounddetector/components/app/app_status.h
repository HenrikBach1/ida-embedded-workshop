#pragma once
// app_status — the handful of app.c values the console reports need (private to app), so
// app_report.c does not reach into app.c's statics.
// Threading: read from the test-console task without the LVGL lock; the values are single
// words, so a read during a change shows either the old or the new one.
#include <stdbool.h>

typedef struct {
    unsigned count;         // loud moments since the last clear
    int      sensitivity;   // 1..10
    bool     dim;           // backlight dimmed after 20 s idle
} app_status_t;

app_status_t app_status(void);
