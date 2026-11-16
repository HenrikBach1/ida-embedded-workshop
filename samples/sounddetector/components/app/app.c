// app.c — the sound detector's state and its inputs. Rules this file keeps:
//   • knob, touch and timer callbacks run on the LVGL task and touch widgets directly;
//   • the microphone task NEVER touches LVGL: on_tick() drains its queue every 100 ms, so there
//     is exactly one owner of the screens and of the event list;
//   • every handler only changes state and marks it dirty; flush() redraws once.
#include "app.h"
#include "app_model.h"
#include "app_monitor.h"
#include "app_nav.h"
#include "app_screens.h"
#include "app_status.h"
#include "ui.h"
#include "knob.h"
#include "backlight.h"
#include "haptic.h"
#include "theme.h"
#include "net_time.h"
#include "settings.h"
#include "detector.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "app";

#define TICK_MS          100u
#define IDLE_MS          20000u   // untouched this long: back to Home and dim
#define ACTIVE_PCT       80u
#define ROOM_LEVEL_MIN_CDB (-12000) // the level meter never reports less (digital silence)
#define HAPTIC_MUTE_MS   400u     // click length + margin: the mic hears the motor
#define HANDLING_MUTE_MS 300u     // a knob detent or a finger on the glass is not room sound
#define KNOB_INJECT_MAX  100      // test hook bound: nobody needs more detents in one command

static event_log_t s_log;
static int  s_sens = DETECTOR_SENS_DEFAULT;
static bool s_mic_ok, s_dim, s_dirty, s_clock_ok, s_learning;
static int  s_learn_pct;
static uint32_t s_prev_idle_ms;
static int64_t  s_boot_wall_s;      // Unix time of power-on; 0 until the clock has synced

static uint64_t uptime_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }

static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Every click is followed by a mute, or the detector would count its own feedback.
static void click(void)
{
    monitor_mute_ms(HAPTIC_MUTE_MS);   // first: the mic task outranks this one
    haptic_click();
}

// ---- drawing ----

static void flush(void)
{
    if (!s_dirty) return;
    s_dirty = false;
    app_model_t m = {
        .log = &s_log, .accent = theme_loud_color(event_log_total(&s_log)),
        .sensitivity = s_sens, .mic_ok = s_mic_ok,
        .learning = s_learning, .learn_pct = s_learn_pct,
        .has_baseline = monitor_has_baseline(), .baseline_cdb = monitor_baseline(),
        .boot_wall_s = s_boot_wall_s,
    };
    nav_set_accent(m.accent);
    screen_home_update(&m);
    screen_events_update(&m);
    screen_settings_update(&m);
}

// ---- brightness ----

static void wake(void)
{
    if (!s_dim) return;
    s_dim = false;
    backlight_set_percent(ACTIVE_PCT);
}

static void go_idle(void)
{
    nav_home();
    s_dim = true;
    backlight_set_percent(BACKLIGHT_MIN_PCT);
}

// ---- what can happen (LVGL task) ----

static void loud_started(uint64_t when_ms, int strength)
{
    event_log_push(&s_log, when_ms, (uint8_t)clamp(strength, 1, DETECTOR_STRENGTH_MAX));
    click();
    s_dirty = true;
    ESP_LOGI(TAG, "loud moment %u, strength %d", event_log_total(&s_log), strength);
}

static void loud_got_stronger(int strength)
{
    event_log_set_newest_strength(&s_log, (uint8_t)clamp(strength, 1, DETECTOR_STRENGTH_MAX));
    s_dirty = true;
}

static void room_learned(void)
{
    settings_set_baseline(monitor_baseline());
    s_dirty = true;
    ESP_LOGI(TAG, "room level learned: %d cdB", monitor_baseline());
}

static void set_sensitivity(int value)
{
    value = clamp(value, DETECTOR_SENS_MIN, DETECTOR_SENS_MAX);
    if (value == s_sens) return;
    s_sens = value;
    monitor_set_sensitivity(value);
    settings_set_sensitivity(value);   // debounced: a slider drag is one flash write
    s_dirty = true;
}

static void relearn(void)
{
    click();
    monitor_request_relearn();
    nav_home();                        // the learning progress is drawn on Home
    s_dirty = true;
}

static void clear_events(void)
{
    event_log_clear(&s_log);
    click();
    s_dirty = true;
}

static void on_knob_step(int dir)
{
    lv_display_trigger_activity(NULL);   // the knob is not an LVGL input device
    monitor_mute_ms(HANDLING_MUTE_MS);   // the detent clicks right next to the microphone
    wake();
    nav_move(dir);
}

static const screen_settings_actions_t SETTINGS_ACTIONS = {
    .on_sensitivity = set_sensitivity, .on_relearn = relearn, .on_clear = clear_events,
};

// ---- 100 ms tick (LVGL task) ----

static void drain_monitor(void)
{
    monitor_msg_t msg;
    while (monitor_poll(&msg)) {
        if (msg.kind == MONITOR_LOUD_START) loud_started(msg.uptime_ms, msg.strength);
        else                                loud_got_stronger(msg.strength);
    }
}

static void poll_learning(void)
{
    bool learning = monitor_is_learning();
    int pct = monitor_learn_percent();
    if (learning == s_learning && pct == s_learn_pct) return;
    if (s_learning && !learning) room_learned();   // seen here, so it cannot be lost in a queue
    s_learning = learning;
    s_learn_pct = pct;
    s_dirty = true;
}

// The first time sync turns "+0:12:34" into clock times on both lists.
static void poll_clock(void)
{
    int64_t wall_s;
    bool ok = net_time_now(&wall_s);
    if (ok == s_clock_ok) return;
    s_clock_ok = ok;
    // Fixed once, so an event keeps the same text on every redraw.
    s_boot_wall_s = ok ? wall_s - (int64_t)(uptime_ms() / 1000u) : 0;
    s_dirty = true;
    ESP_LOGI(TAG, "clock %s", ok ? "synced: showing real times" : "unknown");
}

// LVGL tracks touch activity itself; a drop in the inactive time means "someone touched it".
static void poll_idle(void)
{
    uint32_t idle_ms = lv_display_get_inactive_time(NULL);
    if (idle_ms < s_prev_idle_ms) wake();
    if (idle_ms < 2 * TICK_MS) monitor_mute_ms(HANDLING_MUTE_MS);   // being touched right now
    if (!s_dim && idle_ms >= IDLE_MS) go_idle();
    s_prev_idle_ms = idle_ms;
}

static void on_tick(lv_timer_t *t)
{
    (void)t;
    drain_monitor();
    poll_learning();
    poll_clock();
    poll_idle();
    flush();
}

// ---- test hooks (caller holds the LVGL lock) ----

void app_inject_knob(int steps)
{
    steps = clamp(steps, -KNOB_INJECT_MAX, KNOB_INJECT_MAX);
    int dir = steps < 0 ? -1 : 1;
    for (int i = 0; i < steps * dir; i++) on_knob_step(dir);
}

void app_inject_loud(int strength) { loud_started(uptime_ms(), strength); flush(); }
void app_inject_sens(int value)    { set_sensitivity(value); flush(); }
void app_inject_clear(void)        { clear_events(); flush(); }
void app_inject_relearn(void)      { relearn(); poll_learning(); flush(); }
void app_inject_idle(void)         { go_idle(); }
void app_inject_listen(int on)     { monitor_set_paused(on == 0); }

void app_inject_reset(void)
{
    event_log_clear(&s_log);
    set_sensitivity(DETECTOR_SENS_DEFAULT);
    lv_display_trigger_activity(NULL);
    wake();
    nav_home();
    s_dirty = true;
    flush();
}

// ---- for app_report.c ----

app_status_t app_status(void)
{
    return (app_status_t){ .count = event_log_total(&s_log), .sensitivity = s_sens, .dim = s_dim };
}

// ---- boot ----

static void build_screens(void)
{
    screen_home_create(nav_create_surface(NAV_HOME));
    screen_events_create(nav_create_surface(NAV_EVENTS));
    screen_settings_create(nav_create_surface(NAV_SETTINGS), &SETTINGS_ACTIONS);
    nav_start();
    lv_timer_create(on_tick, TICK_MS, NULL);
    s_dirty = true;
    flush();
}

// Stored values cross a trust boundary (flash): anything out of range is not used.
static int stored_sensitivity(const settings_t *stored)
{
    bool ok = stored->has_sensitivity && stored->sensitivity >= DETECTOR_SENS_MIN &&
              stored->sensitivity <= DETECTOR_SENS_MAX;
    return ok ? stored->sensitivity : DETECTOR_SENS_DEFAULT;
}

static bool stored_room_ok(const settings_t *stored)
{
    if (!stored->has_baseline) return false;
    if (stored->baseline_cdb < 0 && stored->baseline_cdb > ROOM_LEVEL_MIN_CDB) return true;
    ESP_LOGE(TAG, "stored room level %d cdB is out of range: learning again", stored->baseline_cdb);
    return false;
}

void app_init(void)
{
    settings_t stored = settings_get();
    s_sens = stored_sensitivity(&stored);
    bool has_room = stored_room_ok(&stored);

    // The monitor first, so the first frame already knows whether the room is being learned.
    s_mic_ok = monitor_start(has_room, stored.baseline_cdb, s_sens);
    if (!s_mic_ok) ESP_LOGE(TAG, "microphone not started: nothing will be detected");
    s_learning = monitor_is_learning();

    if (!ui_lock(1000)) { ESP_LOGE(TAG, "no LVGL lock — no screen"); return; }
    build_screens();
    ui_unlock();

    knob_init(on_knob_step);   // takes the lock itself
    ESP_LOGI(TAG, "ready: sensitivity %d, room level %s", s_sens,
             has_room ? "from storage" : "being learned");
}
