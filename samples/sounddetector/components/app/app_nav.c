#include "app_nav.h"
#include "tokens.h"
#include "app_widgets.h"

#define SLIDE_MS    200     // the one full-screen animation; the panel has no TE line
#define SLIDE_SETTLE_MS 40  // the animation starts on LVGL's next animation pass, not at once
#define RING_W      6
#define DOT_DIA     8
#define DOT_PITCH   16
#define DOT_TOP_Y   30      // inside the 340 px safe circle (half-chord there is 80 px)

static lv_obj_t *s_surface[NAV_COUNT];
static lv_obj_t *s_ring[NAV_COUNT];
static lv_obj_t *s_active_dot[NAV_COUNT];
static int       s_index;
static bool      s_sliding;        // a slide animation is in flight
static bool      s_home_pending;   // nav_home() arrived during a slide; do it when it lands

static void on_gesture(lv_event_t *e)
{
    (void)e;
    // Dragging the sensitivity slider sideways is not a swipe between screens. Gestures bubble,
    // so the event's target is always the screen: ask which object the finger is on instead.
    lv_obj_t *pressed = lv_indev_get_active_obj();
    if (pressed && lv_obj_check_type(pressed, &lv_slider_class)) return;
    lv_indev_t *indev = lv_indev_active();
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;
    nav_move(dir == LV_DIR_LEFT ? +1 : -1);
    lv_indev_wait_release(indev);   // or the swipe also clicks whatever the finger lifts on
}

static lv_obj_t *make_ring(lv_obj_t *scr)
{
    lv_obj_t *ring = lv_obj_create(scr);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, HUD_SAFE_DIA, HUD_SAFE_DIA);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, RING_W, 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
    lv_obj_set_clickable(ring, false);   // touches fall through to the screen
    return ring;
}

static void make_dots(lv_obj_t *scr, int idx)
{
    for (int i = 0; i < NAV_COUNT; i++) {
        lv_obj_t *dot = widget_circle(scr, DOT_DIA, HUD_OFF);
        lv_obj_align(dot, LV_ALIGN_TOP_MID, (2 * i - (NAV_COUNT - 1)) * DOT_PITCH / 2, DOT_TOP_Y);
        if (i == idx) s_active_dot[idx] = dot;
    }
}

lv_obj_t *nav_create_surface(nav_surface_t idx)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HUD_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);   // the screen never scrolls; the event list does
    lv_obj_set_clickable(scr, true);
    lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, NULL);
    s_ring[idx] = make_ring(scr);
    make_dots(scr, idx);
    s_surface[idx] = scr;
    return scr;
}

void nav_start(void)
{
    lv_obj_t *boot_screen = lv_screen_active();
    s_index = NAV_HOME;
    lv_screen_load(s_surface[NAV_HOME]);
    if (boot_screen != s_surface[NAV_HOME]) lv_obj_delete(boot_screen);
}

static void show(int index, lv_screen_load_anim_t anim);

static void on_slide_done(lv_timer_t *t)
{
    (void)t;                         // one-shot: LVGL deletes the timer after this call
    s_sliding = false;
    if (!s_home_pending) return;
    s_home_pending = false;
    nav_home();
}

// LVGL silently drops a load of the screen that is still "active" while another one is sliding
// in (A -> B -> A within one animation frame), which would leave s_index pointing at a screen
// that is not shown. So only one slide runs at a time; s_index always matches the panel.
static void show(int index, lv_screen_load_anim_t anim)
{
    s_index = index;
    s_sliding = true;
    lv_screen_load_anim(s_surface[index], anim, SLIDE_MS, 0, false);
    lv_timer_t *done = lv_timer_create(on_slide_done, SLIDE_MS + SLIDE_SETTLE_MS, NULL);
    lv_timer_set_repeat_count(done, 1);
}

void nav_move(int dir)
{
    if (s_sliding) return;           // a turn or swipe during a slide is dropped, not queued
    show((s_index + dir + NAV_COUNT) % NAV_COUNT,
         dir > 0 ? LV_SCREEN_LOAD_ANIM_OVER_LEFT : LV_SCREEN_LOAD_ANIM_OVER_RIGHT);
}

void nav_home(void)
{
    if (s_sliding) { s_home_pending = true; return; }
    if (s_index != NAV_HOME) show(NAV_HOME, LV_SCREEN_LOAD_ANIM_OVER_RIGHT);
}

nav_surface_t nav_current(void)
{
    return (nav_surface_t)s_index;
}

void nav_set_accent(uint32_t rgb)
{
    lv_color_t c = lv_color_hex(rgb);
    for (int i = 0; i < NAV_COUNT; i++) {
        lv_obj_set_style_border_color(s_ring[i], c, 0);
        lv_obj_set_style_bg_color(s_active_dot[i], c, 0);
    }
}
