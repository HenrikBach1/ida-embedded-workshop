// Host tests for event_log.c: last 20 loud moments, newest first.
#include "unity.h"
#include "event_log.h"

static event_log_t log_;

void setUp(void)    { log_ = (event_log_t){0}; }
void tearDown(void) {}

void test_empty_log(void)
{
    TEST_ASSERT_EQUAL_UINT(0, event_log_stored(&log_));
    TEST_ASSERT_EQUAL_UINT(0, event_log_total(&log_));
    TEST_ASSERT_NULL(event_log_get(&log_, 0));
}

void test_newest_first(void)
{
    event_log_push(&log_, 1000, 1);
    event_log_push(&log_, 2000, 4);
    event_log_push(&log_, 3000, 2);
    TEST_ASSERT_EQUAL_UINT64(3000, event_log_get(&log_, 0)->uptime_ms);
    TEST_ASSERT_EQUAL_UINT64(2000, event_log_get(&log_, 1)->uptime_ms);
    TEST_ASSERT_EQUAL_UINT64(1000, event_log_get(&log_, 2)->uptime_ms);
    TEST_ASSERT_EQUAL_UINT8(4, event_log_get(&log_, 1)->strength);
    TEST_ASSERT_NULL(event_log_get(&log_, 3));
}

void test_keeps_the_last_twenty_but_counts_them_all(void)
{
    for (unsigned i = 1; i <= 27; i++) event_log_push(&log_, i * 1000u, 1);
    TEST_ASSERT_EQUAL_UINT(EVENT_LOG_CAP, event_log_stored(&log_));
    TEST_ASSERT_EQUAL_UINT(27, event_log_total(&log_));
    TEST_ASSERT_EQUAL_UINT64(27000, event_log_get(&log_, 0)->uptime_ms);
    TEST_ASSERT_EQUAL_UINT64(8000, event_log_get(&log_, EVENT_LOG_CAP - 1)->uptime_ms);
    TEST_ASSERT_NULL(event_log_get(&log_, EVENT_LOG_CAP));
}

void test_newest_strength_can_be_raised(void)
{
    event_log_set_newest_strength(&log_, 5);               // empty: no-op, no crash
    event_log_push(&log_, 1000, 2);
    event_log_push(&log_, 2000, 1);
    event_log_set_newest_strength(&log_, 5);
    TEST_ASSERT_EQUAL_UINT8(5, event_log_get(&log_, 0)->strength);
    TEST_ASSERT_EQUAL_UINT8(2, event_log_get(&log_, 1)->strength);
}

void test_clear_starts_fresh(void)
{
    for (unsigned i = 0; i < 25; i++) event_log_push(&log_, i, 3);
    event_log_clear(&log_);
    TEST_ASSERT_EQUAL_UINT(0, event_log_stored(&log_));
    TEST_ASSERT_EQUAL_UINT(0, event_log_total(&log_));
    TEST_ASSERT_NULL(event_log_get(&log_, 0));
    event_log_push(&log_, 42, 1);
    TEST_ASSERT_EQUAL_UINT64(42, event_log_get(&log_, 0)->uptime_ms);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_log);
    RUN_TEST(test_newest_first);
    RUN_TEST(test_keeps_the_last_twenty_but_counts_them_all);
    RUN_TEST(test_newest_strength_can_be_raised);
    RUN_TEST(test_clear_starts_fresh);
    return UNITY_END();
}
