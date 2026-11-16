#include "detector.h"
#include <math.h>

// Sensitivity 1..10 -> margin over the room level. A quiet room measures about -75 dBFS here and
// nearby speech 20-30 dB above it, so the middle of the scale sits around normal talking.
static const int MARGIN_CDB[DETECTOR_SENS_MAX] = {
    3000, 2700, 2400, 2100, 1800, 1500, 1200, 1000, 800, 600,
};

#define STRENGTH_STEP_CDB   600   // every 6 dB over the threshold adds a dot
#define HOLD_BELOW_CDB      300   // a running event stays alive down to 3 dB under the threshold
#define NORMAL_RANGE_SIGMAS  3     // room level = mean + this many standard deviations

static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void detector_set_baseline(detector_t *d, int baseline_cdb)
{
    d->baseline_cdb = baseline_cdb;
    d->has_baseline = true;
    d->calibrating = false;
}

void detector_start_calibration(detector_t *d)
{
    d->calibrating = true;
    d->cal_started = false;
    d->cal_heard_ms = 0;
    d->cal_sum = 0;
    d->cal_sum_sq = 0;
    d->cal_n = 0;
    d->in_event = false;
}

bool detector_is_calibrating(const detector_t *d) { return d->calibrating; }
bool detector_has_baseline(const detector_t *d)   { return d->has_baseline; }
int  detector_baseline(const detector_t *d)       { return d->has_baseline ? d->baseline_cdb : 0; }
int  detector_strength(const detector_t *d)       { return d->strength; }

int detector_cal_percent(const detector_t *d)
{
    if (!d->calibrating) return 100;
    return d->cal_heard_ms >= DETECTOR_CAL_MS ? 100 : (int)(d->cal_heard_ms * 100u / DETECTOR_CAL_MS);
}

int detector_margin_cdb(int sensitivity)
{
    return MARGIN_CDB[clamp(sensitivity, DETECTOR_SENS_MIN, DETECTOR_SENS_MAX) - 1];
}

// Top of the normal range of what was heard: mean + NORMAL_RANGE_SIGMAS standard deviations.
static int learned_level(const detector_t *d)
{
    double n = (double)d->cal_n;
    double mean = (double)d->cal_sum / n;
    double variance = (double)d->cal_sum_sq / n - mean * mean;
    double sigma = variance > 0.0 ? sqrt(variance) : 0.0;
    return (int)lround(mean + NORMAL_RANGE_SIGMAS * sigma);
}

static detector_out_t feed_calibration(detector_t *d, int level_cdb, uint32_t now_ms, bool muted)
{
    uint32_t since_prev = d->cal_started ? now_ms - d->cal_prev_ms : 0;
    d->cal_started = true;
    d->cal_prev_ms = now_ms;
    if (muted) return DET_NONE;          // not heard: neither sampled nor counted as time
    d->cal_heard_ms += since_prev;
    d->cal_sum += level_cdb;
    d->cal_sum_sq += (int64_t)level_cdb * level_cdb;
    d->cal_n++;
    if (d->cal_heard_ms < DETECTOR_CAL_MS) return DET_NONE;
    detector_set_baseline(d, learned_level(d));
    return DET_CAL_DONE;
}

static detector_out_t feed_loud(detector_t *d, int over_cdb, uint32_t now_ms)
{
    int s = clamp(over_cdb / STRENGTH_STEP_CDB + 1, 1, DETECTOR_STRENGTH_MAX);
    d->last_loud_ms = now_ms;
    if (!d->in_event) {
        d->in_event = true;
        d->strength = s;
        return DET_EVENT_START;
    }
    if (s <= d->strength) return DET_NONE;
    d->strength = s;
    return DET_EVENT_STRONGER;
}

static detector_out_t feed_quiet(detector_t *d, int over_cdb, uint32_t now_ms)
{
    if (!d->in_event) return DET_NONE;
    if (over_cdb > -HOLD_BELOW_CDB) { d->last_loud_ms = now_ms; return DET_NONE; }
    if (now_ms - d->last_loud_ms < DETECTOR_QUIET_MS) return DET_NONE;
    d->in_event = false;
    return DET_EVENT_END;
}

detector_out_t detector_feed(detector_t *d, int level_cdb, uint32_t now_ms, int sensitivity,
                             bool muted)
{
    if (d->calibrating) return feed_calibration(d, level_cdb, now_ms, muted);
    if (muted) return DET_NONE;
    if (!d->has_baseline) return DET_NONE;

    int over = level_cdb - (d->baseline_cdb + detector_margin_cdb(sensitivity));
    return over > 0 ? feed_loud(d, over, now_ms) : feed_quiet(d, over, now_ms);
}
