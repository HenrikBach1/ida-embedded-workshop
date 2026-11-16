#pragma once
// app_nav — moving between the three screens (private to app). Owns the surfaces, their rim
// ring and position dots, and the ONE gesture handler. Model from the round-display UX skill:
// a short wrapping ring of screens; turning the knob and swiping left/right do the same thing;
// neither ever commits anything.
// Threading: LVGL lock held for every call.
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef enum { NAV_HOME = 0, NAV_EVENTS, NAV_SETTINGS, NAV_COUNT } nav_surface_t;

// Create surface `idx`: a screen with background, rim ring, position dots and swipe handling.
// Create all NAV_COUNT of them, fill them, then call nav_start().
lv_obj_t *nav_create_surface(nav_surface_t idx);

// Show Home without animation and delete the screen LVGL started with.
void nav_start(void);

// +1 = next screen, -1 = previous; wraps around. Ignored while a slide is still running.
void nav_move(int dir);

// Slide back to Home (no-op when already there). During a slide it is done when that lands.
void nav_home(void);

nav_surface_t nav_current(void);

// Recolour the ring and the active position dot on every surface.
void nav_set_accent(uint32_t rgb);
