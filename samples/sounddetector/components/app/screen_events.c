// screen_events.c — the last 20 loud moments. The rows are created once and reused, so drawing
// the list never allocates; the list container is the only thing on this screen that scrolls.
#include "app_screens.h"
#include "app_widgets.h"
#include "event_time.h"
#include "tokens.h"
#include "fonts.h"
#include "esp_log.h"

static const char *TAG = "screen_events";

#define LIST_W   230    // a 230 x 200 box centred 10 px low fits the safe circle at every row
#define LIST_H   200
#define ROW_H    40     // font_muli_28 has a 34 px line box
#define DOT_DIA   9
#define DOT_PITCH 14

static lv_obj_t *s_list, *s_empty;
static unsigned  s_shown_total;   // only a NEW event may pull the list back to the top
static lv_obj_t *s_row[EVENT_LOG_CAP], *s_when[EVENT_LOG_CAP];
static lv_obj_t *s_dot[EVENT_LOG_CAP][WIDGET_STRENGTH_MAX];

static lv_obj_t *make_list(lv_obj_t *scr)
{
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LIST_W, LIST_H);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);              // horizontal stays the swipe between screens
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    return list;
}

static void make_row(int i)
{
    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LIST_W, ROW_H);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(HUD_LINE), 0);
    lv_obj_set_clickable(row, false);                     // presses go to the list, which scrolls
    lv_obj_set_scrollable(row, false);
    s_row[i] = row;
    s_when[i] = widget_label(row, &font_muli_28, HUD_INK, LV_ALIGN_LEFT_MID, 0);
    // The strip is centred on its middle dot: two pitches plus half a dot in from the right edge.
    widget_dots_create(row, s_dot[i], DOT_DIA, DOT_PITCH, LV_ALIGN_RIGHT_MID,
                       -(2 * DOT_PITCH + 2), 0);
}

void screen_events_create(lv_obj_t *scr)
{
    lv_obj_t *title = widget_label(scr, LV_FONT_DEFAULT, HUD_FAINT, LV_ALIGN_CENTER, -110);
    lv_label_set_text(title, "LAST 20");
    s_list = make_list(scr);
    for (int i = 0; i < EVENT_LOG_CAP; i++) make_row(i);
    s_empty = widget_label(scr, LV_FONT_DEFAULT, HUD_SUB, LV_ALIGN_CENTER, 0);
    lv_label_set_text(s_empty, "no loud moments yet");
}

static void fill_row(int i, const app_model_t *m, const event_entry_t *e)
{
    char when[EVENT_TIME_TEXT_MAX];
    if (!event_time_format(when, sizeof(when), e->uptime_ms, m->boot_wall_s))
        ESP_LOGE(TAG, "event time did not fit in %u bytes", (unsigned)sizeof(when));
    lv_label_set_text(s_when[i], when);
    widget_dots_set(s_dot[i], e->strength, m->accent);
}

void screen_events_update(const app_model_t *m)
{
    for (int i = 0; i < EVENT_LOG_CAP; i++) {
        const event_entry_t *e = event_log_get(m->log, (unsigned)i);
        lv_obj_set_hidden(s_row[i], e == NULL);
        if (e) fill_row(i, m, e);
    }
    lv_obj_set_hidden(s_empty, event_log_stored(m->log) != 0);
    unsigned total = event_log_total(m->log);
    if (total != s_shown_total) lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);   // newest is on top
    s_shown_total = total;
}
