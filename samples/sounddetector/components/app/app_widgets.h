#pragma once
// app_widgets — the few LVGL shapes all three screens share (private to app). Colours are
// 0xRRGGBB values from the theme tokens; nothing here knows about the app's state.
// Threading: LVGL lock held, like every function that touches a widget.
#include <stdint.h>
#include "lvgl.h"

#define WIDGET_STRENGTH_MAX 5

// A label in `font` and colour `rgb`, aligned on its parent with a vertical offset.
lv_obj_t *widget_label(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, lv_align_t align,
                       int y);

// A filled circle of `dia` px that lets touches fall through.
lv_obj_t *widget_circle(lv_obj_t *parent, int dia, uint32_t rgb);

// A pill button `w` px wide at offset (x, y) from the centre of `parent`, with a text label in
// the default font. Returns the button; attach SHORT_CLICKED or LONG_PRESSED to it.
lv_obj_t *widget_button(lv_obj_t *parent, const char *text, int w, int x, int y);

// Five strength dots of `dia` px, `pitch` px apart, in a row whose centre sits at (x, y) from
// `align` on `parent`. dots[] receives the five objects for widget_dots_set().
void widget_dots_create(lv_obj_t *parent, lv_obj_t *dots[WIDGET_STRENGTH_MAX], int dia, int pitch,
                        lv_align_t align, int x, int y);

// Light the first `strength` dots in `rgb`; the rest take the "off" colour.
void widget_dots_set(lv_obj_t *const dots[WIDGET_STRENGTH_MAX], int strength, uint32_t rgb);
