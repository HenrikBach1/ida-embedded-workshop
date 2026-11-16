// screen_home.c — the at-a-glance screen. All positions are offsets from the centre of the
// 360 px panel and stay inside the 340 px safe circle.
#include "app_screens.h"
#include "app_widgets.h"
#include "event_time.h"
#include "tokens.h"
#include "fonts.h"
#include "esp_log.h"

static const char *TAG = "screen_home";

#define DOT_DIA     16
#define DOT_PITCH   26

static lv_obj_t *s_title, *s_main, *s_sub, *s_hint, *s_arc;
static lv_obj_t *s_dot[WIDGET_STRENGTH_MAX];

static lv_obj_t *make_progress_arc(lv_obj_t *scr)
{
    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_set_size(arc, HUD_SAFE_DIA - 24, HUD_SAFE_DIA - 24);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(HUD_LINE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(HUD_INK), LV_PART_INDICATOR);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_hidden(arc, true);
    return arc;
}

void screen_home_create(lv_obj_t *scr)
{
    s_arc   = make_progress_arc(scr);
    s_title = widget_label(scr, LV_FONT_DEFAULT, HUD_FAINT, LV_ALIGN_CENTER, -84);
    s_main  = widget_label(scr, &font_muli_28,   HUD_INK,   LV_ALIGN_CENTER, -38);
    s_sub   = widget_label(scr, LV_FONT_DEFAULT, HUD_SUB,   LV_ALIGN_CENTER, 50);
    s_hint  = widget_label(scr, LV_FONT_DEFAULT, HUD_FAINT, LV_ALIGN_CENTER, 76);
    widget_dots_create(scr, s_dot, DOT_DIA, DOT_PITCH, LV_ALIGN_CENTER, 0, 10);
}

static void set_texts(const char *title, const char *main, const char *sub, const char *hint)
{
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_main, main);
    lv_label_set_text(s_sub, sub);
    lv_label_set_text(s_hint, hint);
}

static void show_last_event(const app_model_t *m, const event_entry_t *last)
{
    char when[EVENT_TIME_TEXT_MAX];
    if (!event_time_format(when, sizeof(when), last->uptime_ms, m->boot_wall_s))
        ESP_LOGE(TAG, "event time did not fit in %u bytes", (unsigned)sizeof(when));
    unsigned total = event_log_total(m->log);
    char count[32];
    lv_snprintf(count, sizeof(count), total == 1 ? "%u loud moment" : "%u loud moments", total);
    set_texts("LAST LOUD MOMENT", when, count, m->boot_wall_s > 0 ? "" : "time since power-on");
    widget_dots_set(s_dot, last->strength, m->accent);
}

void screen_home_update(const app_model_t *m)
{
    lv_obj_set_hidden(s_arc, !m->learning);
    if (!m->mic_ok) {
        set_texts("SOUND DETECTOR", "mic error", "the microphone did not start", "");
        widget_dots_set(s_dot, 0, m->accent);
        return;
    }
    if (m->learning) {
        lv_arc_set_value(s_arc, m->learn_pct);
        set_texts("LEARNING THE ROOM", "listening", "keep it normal for 5 seconds", "");
        widget_dots_set(s_dot, 0, m->accent);
        return;
    }
    const event_entry_t *last = event_log_get(m->log, 0);
    if (last) { show_last_event(m, last); return; }
    set_texts("SOUND DETECTOR", "all quiet", "no loud moments", "");
    widget_dots_set(s_dot, 0, m->accent);
}
