// Host tests for detector.c: learning the room, sensitivity, one event per noise, strength.
#include "unity.h"
#include "detector.h"

#define ROOM   (-7500)          // quiet room, centi-dBFS
#define SENS   DETECTOR_SENS_DEFAULT
#define STEP   10u              // one block = 10 ms

static detector_t d;
static uint32_t   now;

void setUp(void)    { d = (detector_t){0}; now = 1000; }
void tearDown(void) {}

static detector_out_t feed(int level)
{
    detector_out_t out = detector_feed(&d, level, now, SENS, false);
    now += STEP;
    return out;
}

// Feed `ms` of a constant level; return how many blocks reported `want`.
static int feed_for(int level, uint32_t ms, detector_out_t want)
{
    int hits = 0;
    for (uint32_t t = 0; t < ms; t += STEP) if (feed(level) == want) hits++;
    return hits;
}

static int thr(void) { return ROOM + detector_margin_cdb(SENS); }

void test_zero_init_detects_nothing(void)
{
    TEST_ASSERT_EQUAL_INT(0, feed_for(0, 3000, DET_EVENT_START));
    TEST_ASSERT_FALSE(detector_has_baseline(&d));
}

void test_learning_takes_five_seconds_and_finds_the_room(void)
{
    detector_start_calibration(&d);
    TEST_ASSERT_EQUAL_INT(0, feed_for(ROOM, DETECTOR_CAL_MS, DET_CAL_DONE));
    TEST_ASSERT_TRUE(detector_is_calibrating(&d));
    TEST_ASSERT_EQUAL_INT(DET_CAL_DONE, feed(ROOM));
    TEST_ASSERT_FALSE(detector_is_calibrating(&d));
    TEST_ASSERT_EQUAL_INT(ROOM, detector_baseline(&d));
}

void test_a_restless_room_learns_a_higher_level_than_its_average(void)
{
    // Blocks alternate 2 dB under and over -75 dBFS: mean -75, sigma 2 dB -> level -75 + 6 dB.
    detector_start_calibration(&d);
    detector_out_t out = DET_NONE;
    for (int i = 0; out != DET_CAL_DONE; i++) out = feed(i % 2 ? ROOM + 200 : ROOM - 200);
    TEST_ASSERT_INT_WITHIN(5, ROOM + 600, detector_baseline(&d));
}

void test_learning_reports_progress(void)
{
    detector_start_calibration(&d);
    TEST_ASSERT_EQUAL_INT(0, detector_cal_percent(&d));
    feed_for(ROOM, 2500, DET_NONE);
    TEST_ASSERT_INT_WITHIN(1, 50, detector_cal_percent(&d));
    feed_for(ROOM, 3000, DET_NONE);
    TEST_ASSERT_EQUAL_INT(100, detector_cal_percent(&d));
}

void test_nothing_counts_while_learning(void)
{
    detector_start_calibration(&d);
    TEST_ASSERT_EQUAL_INT(0, feed_for(-2000, 2000, DET_EVENT_START));
}

void test_muted_blocks_do_not_shorten_or_feed_learning(void)
{
    detector_start_calibration(&d);
    for (int i = 0; i < 300; i++) {                       // 3 s of muted, loud blocks
        TEST_ASSERT_EQUAL_INT(DET_NONE, detector_feed(&d, -2000, now, SENS, true));
        now += STEP;
    }
    // Still the full five seconds of heard blocks (give or take the one block boundary).
    TEST_ASSERT_EQUAL_INT(0, feed_for(ROOM, DETECTOR_CAL_MS - STEP, DET_CAL_DONE));
    TEST_ASSERT_EQUAL_INT(1, feed_for(ROOM, 2 * STEP, DET_CAL_DONE));
    TEST_ASSERT_EQUAL_INT(ROOM, detector_baseline(&d));
}

void test_a_pause_in_the_middle_of_learning_does_not_count_as_listening(void)
{
    detector_start_calibration(&d);
    feed_for(ROOM, 2000, DET_NONE);
    for (int i = 0; i < 1000; i++) {                      // 10 s not heard, and loud
        TEST_ASSERT_EQUAL_INT(DET_NONE, detector_feed(&d, -2000, now, SENS, true));
        now += STEP;
    }
    TEST_ASSERT_INT_WITHIN(1, 40, detector_cal_percent(&d));
    TEST_ASSERT_EQUAL_INT(0, feed_for(ROOM, 2900, DET_CAL_DONE));
    TEST_ASSERT_EQUAL_INT(1, feed_for(ROOM, 200, DET_CAL_DONE));
    TEST_ASSERT_EQUAL_INT(ROOM, detector_baseline(&d));
}

void test_a_muted_stretch_inside_an_event_does_not_split_it(void)
{
    detector_set_baseline(&d, ROOM);
    TEST_ASSERT_EQUAL_INT(DET_EVENT_START, feed(thr() + 100));
    for (int i = 0; i < 40; i++) {                        // the 400 ms after the haptic click
        TEST_ASSERT_EQUAL_INT(DET_NONE, detector_feed(&d, -1000, now, SENS, true));
        now += STEP;
    }
    TEST_ASSERT_EQUAL_INT(0, feed_for(thr() + 100, 2000, DET_EVENT_START));
}

void test_learning_survives_a_clock_wrap(void)
{
    now = UINT32_MAX - 2000;
    detector_start_calibration(&d);
    feed_for(ROOM, DETECTOR_CAL_MS, DET_NONE);
    TEST_ASSERT_EQUAL_INT(DET_CAL_DONE, feed(ROOM));
}

void test_sound_at_the_threshold_is_not_loud_one_above_is(void)
{
    detector_set_baseline(&d, ROOM);
    TEST_ASSERT_EQUAL_INT(DET_NONE, feed(thr()));
    TEST_ASSERT_EQUAL_INT(DET_EVENT_START, feed(thr() + 1));
}

void test_long_noise_is_one_event(void)
{
    detector_set_baseline(&d, ROOM);
    TEST_ASSERT_EQUAL_INT(1, feed_for(thr() + 100, 10000, DET_EVENT_START));
}

void test_stuttering_noise_is_one_event(void)
{
    detector_set_baseline(&d, ROOM);
    int starts = 0;
    for (int burst = 0; burst < 10; burst++) {            // 200 ms loud, 1 s quiet, ten times
        starts += feed_for(thr() + 100, 200, DET_EVENT_START);
        starts += feed_for(ROOM, 1000, DET_EVENT_START);
    }
    TEST_ASSERT_EQUAL_INT(1, starts);
}

void test_event_ends_after_quiet_and_the_next_is_new(void)
{
    detector_set_baseline(&d, ROOM);
    TEST_ASSERT_EQUAL_INT(DET_EVENT_START, feed(thr() + 100));
    TEST_ASSERT_EQUAL_INT(0, feed_for(ROOM, DETECTOR_QUIET_MS - STEP, DET_EVENT_END));
    TEST_ASSERT_EQUAL_INT(DET_EVENT_END, feed(ROOM));
    TEST_ASSERT_EQUAL_INT(DET_EVENT_START, feed(thr() + 100));
}

void test_just_under_the_threshold_keeps_an_event_alive_but_never_starts_one(void)
{
    detector_set_baseline(&d, ROOM);
    TEST_ASSERT_EQUAL_INT(0, feed_for(thr() - 100, 3000, DET_EVENT_START));
    feed(thr() + 100);
    TEST_ASSERT_EQUAL_INT(0, feed_for(thr() - 100, 3000, DET_EVENT_END));
}

void test_strength_is_one_dot_per_6_db_capped_at_five(void)
{
    static const struct { int over; int dots; } CASES[] = {
        { 1, 1 }, { 599, 1 }, { 600, 2 }, { 1200, 3 }, { 1800, 4 }, { 2400, 5 }, { 6000, 5 },
    };
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        setUp();
        detector_set_baseline(&d, ROOM);
        feed(thr() + CASES[i].over);
        TEST_ASSERT_EQUAL_INT_MESSAGE(CASES[i].dots, detector_strength(&d), "over-threshold case");
    }
}

void test_event_reports_when_it_gets_stronger_and_never_weaker(void)
{
    detector_set_baseline(&d, ROOM);
    TEST_ASSERT_EQUAL_INT(DET_EVENT_START, feed(thr() + 100));
    TEST_ASSERT_EQUAL_INT(DET_EVENT_STRONGER, feed(thr() + 1300));
    TEST_ASSERT_EQUAL_INT(3, detector_strength(&d));
    TEST_ASSERT_EQUAL_INT(DET_NONE, feed(thr() + 100));
    TEST_ASSERT_EQUAL_INT(3, detector_strength(&d));
}

void test_higher_sensitivity_needs_less_sound(void)
{
    for (int s = DETECTOR_SENS_MIN; s < DETECTOR_SENS_MAX; s++)
        TEST_ASSERT_TRUE(detector_margin_cdb(s + 1) < detector_margin_cdb(s));
    TEST_ASSERT_EQUAL_INT(detector_margin_cdb(1), detector_margin_cdb(-4));
    TEST_ASSERT_EQUAL_INT(detector_margin_cdb(10), detector_margin_cdb(99));
}

void test_muted_blocks_are_ignored(void)
{
    detector_set_baseline(&d, ROOM);
    TEST_ASSERT_EQUAL_INT(DET_NONE, detector_feed(&d, -1000, now, SENS, true));
    TEST_ASSERT_EQUAL_INT(0, detector_strength(&d));
}

void test_relearning_drops_the_running_event(void)
{
    detector_set_baseline(&d, ROOM);
    feed(thr() + 100);
    detector_start_calibration(&d);
    TEST_ASSERT_EQUAL_INT(ROOM, detector_baseline(&d));     // old level still readable
    feed_for(ROOM + 500, DETECTOR_CAL_MS, DET_NONE);
    TEST_ASSERT_EQUAL_INT(DET_CAL_DONE, feed(ROOM + 500));
    TEST_ASSERT_EQUAL_INT(ROOM + 500, detector_baseline(&d));
    TEST_ASSERT_EQUAL_INT(0, feed_for(ROOM + 500, 3000, DET_EVENT_END));
}

void test_clock_wrap_does_not_break_the_quiet_timer(void)
{
    detector_set_baseline(&d, ROOM);
    now = UINT32_MAX - 500;
    feed(thr() + 100);
    TEST_ASSERT_EQUAL_INT(1, feed_for(ROOM, 2000, DET_EVENT_END));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_zero_init_detects_nothing);
    RUN_TEST(test_learning_takes_five_seconds_and_finds_the_room);
    RUN_TEST(test_a_restless_room_learns_a_higher_level_than_its_average);
    RUN_TEST(test_learning_reports_progress);
    RUN_TEST(test_nothing_counts_while_learning);
    RUN_TEST(test_muted_blocks_do_not_shorten_or_feed_learning);
    RUN_TEST(test_a_pause_in_the_middle_of_learning_does_not_count_as_listening);
    RUN_TEST(test_a_muted_stretch_inside_an_event_does_not_split_it);
    RUN_TEST(test_learning_survives_a_clock_wrap);
    RUN_TEST(test_sound_at_the_threshold_is_not_loud_one_above_is);
    RUN_TEST(test_long_noise_is_one_event);
    RUN_TEST(test_stuttering_noise_is_one_event);
    RUN_TEST(test_event_ends_after_quiet_and_the_next_is_new);
    RUN_TEST(test_just_under_the_threshold_keeps_an_event_alive_but_never_starts_one);
    RUN_TEST(test_strength_is_one_dot_per_6_db_capped_at_five);
    RUN_TEST(test_event_reports_when_it_gets_stronger_and_never_weaker);
    RUN_TEST(test_higher_sensitivity_needs_less_sound);
    RUN_TEST(test_muted_blocks_are_ignored);
    RUN_TEST(test_relearning_drops_the_running_event);
    RUN_TEST(test_clock_wrap_does_not_break_the_quiet_timer);
    return UNITY_END();
}
