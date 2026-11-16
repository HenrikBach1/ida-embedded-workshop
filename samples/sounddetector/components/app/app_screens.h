#pragma once
// app_screens — the content of the three surfaces (private to app). Each screen is built once on
// a surface from app_nav and then only redrawn from an app_model_t; a screen never reads state
// from anywhere else and never includes net, mic or settings.
// Threading: LVGL lock held for every call. Model pointers are valid only during the call.
#include "lvgl.h"
#include "app_model.h"

// Home: when the last loud moment was, how strong (five dots), how many so far. Shows the
// learning progress while the room is being learned.
void screen_home_create(lv_obj_t *scr);
void screen_home_update(const app_model_t *m);

// Events: the last 20 loud moments, newest first, scrolled with a finger.
void screen_events_create(lv_obj_t *scr);
void screen_events_update(const app_model_t *m);

// What the settings screen asks the app to do. Called on the LVGL task; must not block.
typedef struct {
    void (*on_sensitivity)(int value);   // slider moved to 1..10
    void (*on_relearn)(void);            // "relearn" tapped
    void (*on_clear)(void);              // "hold to clear" held
} screen_settings_actions_t;

// Settings: sensitivity slider, relearn, clear. `actions` must stay valid forever (static).
void screen_settings_create(lv_obj_t *scr, const screen_settings_actions_t *actions);
void screen_settings_update(const app_model_t *m);
