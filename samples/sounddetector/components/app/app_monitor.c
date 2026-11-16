// app_monitor.c — detector on the mic task, results out through a queue + atomics. The mic task
// owns s_det; every other task talks to it through the atomics below, so there is no lock.
#include "app_monitor.h"
#include "detector.h"
#include "mic.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdatomic.h>

static const char *TAG = "monitor";

#define QUEUE_LEN          8
#define SELFTEST_QUIET_MS  400u
#define SELFTEST_STIM_MS   500u
#define SELFTEST_MUTE_MS   1500u

static detector_t    s_det;                 // mic task only, once monitor_start() has returned
static QueueHandle_t s_queue;

// requests into the mic task
static atomic_int  s_sens;
static atomic_bool s_relearn_req, s_peak_reset, s_paused;
static atomic_uint s_mute_req_ms;           // 0 = no request
// reports out of the mic task
static atomic_int  s_level, s_peak, s_baseline, s_learn_pct;
static atomic_bool s_learning, s_has_baseline;

static void post(monitor_kind_t kind)
{
    monitor_msg_t msg = { .kind = kind, .strength = detector_strength(&s_det),
                          .uptime_ms = (uint64_t)(esp_timer_get_time() / 1000) };
    if (xQueueSend(s_queue, &msg, 0) != pdTRUE)
        ESP_LOGE(TAG, "queue full (%d): dropped message kind %d", QUEUE_LEN, (int)kind);
}

// True while a mute requested through monitor_mute_ms() is running.
static bool update_mute(uint32_t now_ms)
{
    static bool     muted;
    static uint32_t until_ms;
    uint32_t req = atomic_exchange(&s_mute_req_ms, 0);
    if (req) {   // a new request may lengthen a running mute, never shorten it
        uint32_t want = now_ms + req;
        if (!muted || (int32_t)(want - until_ms) > 0) until_ms = want;
        muted = true;
    }
    if (muted && (int32_t)(now_ms - until_ms) >= 0) muted = false;
    return muted;
}

static void publish(int level_cdb)
{
    s_level = level_cdb;
    if (atomic_exchange(&s_peak_reset, false) || level_cdb > atomic_load(&s_peak)) s_peak = level_cdb;
    s_learning = detector_is_calibrating(&s_det);
    s_learn_pct = detector_cal_percent(&s_det);
    s_has_baseline = detector_has_baseline(&s_det);
    s_baseline = detector_baseline(&s_det);
}

// Mic task, every 10 ms.
static void on_block(int level_cdb, uint32_t now_ms)
{
    if (atomic_exchange(&s_relearn_req, false)) detector_start_calibration(&s_det);
    bool muted = update_mute(now_ms) || atomic_load(&s_paused);
    detector_out_t out = detector_feed(&s_det, level_cdb, now_ms, atomic_load(&s_sens), muted);
    publish(level_cdb);

    static const struct { bool post; monitor_kind_t kind; } MAP[] = {
        [DET_NONE]           = { false, 0 },
        [DET_CAL_DONE]       = { false, 0 },   // the app sees monitor_is_learning() go false
        [DET_EVENT_START]    = { true,  MONITOR_LOUD_START },
        [DET_EVENT_STRONGER] = { true,  MONITOR_LOUD_STRONGER },
        [DET_EVENT_END]      = { false, 0 },
    };
    if (MAP[out].post) post(MAP[out].kind);
}

bool monitor_start(bool has_baseline, int baseline_cdb, int sensitivity)
{
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(monitor_msg_t));
    if (!s_queue) { ESP_LOGE(TAG, "no queue"); return false; }
    s_sens = sensitivity;
    if (has_baseline) detector_set_baseline(&s_det, baseline_cdb);
    else              detector_start_calibration(&s_det);
    s_has_baseline = has_baseline;
    s_baseline = has_baseline ? baseline_cdb : 0;
    s_learning = !has_baseline;
    s_peak_reset = true;                    // the first block seeds the peak (0 dBFS is not a level)
    ESP_LOGI(TAG, "start: %s, sensitivity %d", has_baseline ? "stored room level" : "learning",
             sensitivity);
    return mic_init(on_block);
}

bool monitor_poll(monitor_msg_t *out)
{
    return s_queue && xQueueReceive(s_queue, out, 0) == pdTRUE;
}

void monitor_set_sensitivity(int sensitivity) { s_sens = sensitivity; }
void monitor_request_relearn(void)            { s_relearn_req = true; }
void monitor_mute_ms(uint32_t ms)             { s_mute_req_ms = ms; }
void monitor_set_paused(bool paused)          { s_paused = paused; }
bool monitor_is_paused(void)                  { return atomic_load(&s_paused); }

// A pending request already counts, so the screen does not flicker back for one tick.
bool monitor_is_learning(void)   { return atomic_load(&s_relearn_req) || atomic_load(&s_learning); }
int  monitor_learn_percent(void) { return atomic_load(&s_relearn_req) ? 0 : atomic_load(&s_learn_pct); }
bool monitor_has_baseline(void)  { return atomic_load(&s_has_baseline); }
int  monitor_baseline(void)      { return atomic_load(&s_baseline); }

void monitor_levels(int *level_cdb, int *baseline_cdb, int *threshold_cdb)
{
    *level_cdb = atomic_load(&s_level);
    *baseline_cdb = atomic_load(&s_baseline);
    *threshold_cdb = *baseline_cdb + detector_margin_cdb(atomic_load(&s_sens));
}

int monitor_take_peak(void)
{
    int peak = atomic_load(&s_peak);
    s_peak_reset = true;
    return peak;
}

static int peak_over(uint32_t ms)
{
    s_peak_reset = true;
    vTaskDelay(pdMS_TO_TICKS(ms));
    return atomic_load(&s_peak);
}

void monitor_selftest(void (*stimulus)(void), int *quiet_cdb, int *stimulus_cdb)
{
    monitor_mute_ms(SELFTEST_MUTE_MS);
    *quiet_cdb = peak_over(SELFTEST_QUIET_MS);
    s_peak_reset = true;
    vTaskDelay(pdMS_TO_TICKS(30));          // let the mic task take the reset before the stimulus
    stimulus();
    vTaskDelay(pdMS_TO_TICKS(SELFTEST_STIM_MS));
    *stimulus_cdb = atomic_load(&s_peak);
}
