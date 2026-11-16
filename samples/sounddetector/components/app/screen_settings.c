// screen_settings.c — sensitivity, relearn, clear. Taps and holds are the only commits; the
// slider is dragged with a finger (the knob only moves between screens).
#include "app_screens.h"
#include "app_widgets.h"
#include "tokens.h"
#include "fonts.h"

#define SENS_MIN  1
#define SENS_MAX  10

static const screen_settings_actions_t *s_actions;
static lv_obj_t *s_slider, *s_value, *s_room;

static void on_slider(lv_event_t *e)
{
    s_actions->on_sensitivity((int)lv_slider_get_value(lv_event_get_target_obj(e)));
}

static void on_relearn(lv_event_t *e) { (void)e; s_actions->on_relearn(); }
static void on_clear(lv_event_t *e)   { (void)e; s_actions->on_clear(); }

static lv_obj_t *make_slider(lv_obj_t *scr)
{
    lv_obj_t *s = lv_slider_create(scr);
    lv_obj_set_size(s, 190, 14);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, -6);
    lv_slider_set_range(s, SENS_MIN, SENS_MAX);
    lv_obj_set_ext_click_area(s, 18);                      // a 14 px track is too thin for a thumb
    lv_obj_set_style_bg_color(s, lv_color_hex(HUD_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(HUD_INK), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(s, on_slider, LV_EVENT_VALUE_CHANGED, NULL);
    return s;
}

void screen_settings_create(lv_obj_t *scr, const screen_settings_actions_t *actions)
{
    s_actions = actions;
    lv_obj_t *title = widget_label(scr, LV_FONT_DEFAULT, HUD_FAINT, LV_ALIGN_CENTER, -96);
    lv_label_set_text(title, "SENSITIVITY");
    s_value  = widget_label(scr, &font_muli_28, HUD_INK, LV_ALIGN_CENTER, -54);
    s_slider = make_slider(scr);
    s_room   = widget_label(scr, LV_FONT_DEFAULT, HUD_FAINT, LV_ALIGN_CENTER, 104);

    lv_obj_t *relearn = widget_button(scr, "relearn", 96, -64, 54);
    lv_obj_add_event_cb(relearn, on_relearn, LV_EVENT_SHORT_CLICKED, NULL);
    // A hold, not a tap: one stray touch while swiping must not wipe the list.
    lv_obj_t *clear = widget_button(scr, "hold to clear", 120, 50, 54);
    lv_obj_add_event_cb(clear, on_clear, LV_EVENT_LONG_PRESSED, NULL);
}

void screen_settings_update(const app_model_t *m)
{
    lv_label_set_text_fmt(s_value, "%d", m->sensitivity);
    if (lv_slider_get_value(s_slider) != m->sensitivity)
        lv_slider_set_value(s_slider, m->sensitivity, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider, lv_color_hex(m->accent), LV_PART_INDICATOR);

    if (m->learning)           lv_label_set_text(s_room, "learning the room...");
    else if (!m->has_baseline) lv_label_set_text(s_room, "room not learned");
    else lv_label_set_text_fmt(s_room, "room level %d dB", m->baseline_cdb / 100);
}
