// Host tests for event_time.c: clock time with a time sync, time since power-on without.
#define _POSIX_C_SOURCE 200809L
#include "unity.h"
#include "event_time.h"
#include <stdlib.h>
#include <time.h>

static char buf[EVENT_TIME_TEXT_MAX];

void setUp(void)    { setenv("TZ", "UTC0", 1); tzset(); }
void tearDown(void) {}

#define NOON 1794830400   // 2026-11-16 12:00:00 UTC

void test_without_a_clock_shows_time_since_power_on(void)
{
    TEST_ASSERT_TRUE(event_time_format(buf, sizeof(buf), 0, 0));
    TEST_ASSERT_EQUAL_STRING("+0:00:00", buf);
    TEST_ASSERT_TRUE(event_time_format(buf, sizeof(buf), 3725999, 0));
    TEST_ASSERT_EQUAL_STRING("+1:02:05", buf);
}

void test_long_uptime_still_fits(void)
{
    uint64_t forty_nine_days = 49ull * 24 * 3600 * 1000 + 59 * 60000 + 59000;
    TEST_ASSERT_TRUE(event_time_format(buf, sizeof(buf), forty_nine_days, 0));
    TEST_ASSERT_EQUAL_STRING("+1176:59:59", buf);
}

void test_with_a_clock_shows_when_it_happened(void)
{
    // Switched on at noon; the event came 90.9 s later.
    TEST_ASSERT_TRUE(event_time_format(buf, sizeof(buf), 90900, NOON));
    TEST_ASSERT_EQUAL_STRING("12:01:30", buf);
}

void test_event_before_the_time_sync_gets_a_real_time_too(void)
{
    // The clock synced an hour after power-on; an event from second 2 still gets its real time.
    TEST_ASSERT_TRUE(event_time_format(buf, sizeof(buf), 2000, NOON));
    TEST_ASSERT_EQUAL_STRING("12:00:02", buf);
}

void test_local_time_zone_is_applied(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
    TEST_ASSERT_TRUE(event_time_format(buf, sizeof(buf), 0, NOON));   // winter: UTC+1
    TEST_ASSERT_EQUAL_STRING("13:00:00", buf);
}

void test_too_small_a_buffer_fails_with_empty_text(void)
{
    char tiny[5] = "xxxx";
    TEST_ASSERT_FALSE(event_time_format(tiny, sizeof(tiny), 1000, 0));
    TEST_ASSERT_EQUAL_STRING("", tiny);
    TEST_ASSERT_FALSE(event_time_format(tiny, 0, 1000, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_without_a_clock_shows_time_since_power_on);
    RUN_TEST(test_long_uptime_still_fits);
    RUN_TEST(test_with_a_clock_shows_when_it_happened);
    RUN_TEST(test_event_before_the_time_sync_gets_a_real_time_too);
    RUN_TEST(test_local_time_zone_is_applied);
    RUN_TEST(test_too_small_a_buffer_fails_with_empty_text);
    return UNITY_END();
}
