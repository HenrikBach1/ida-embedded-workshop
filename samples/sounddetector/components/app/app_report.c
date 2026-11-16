// app_report.c — the one-line reports the test console prints (declared in app.h). Kept apart
// from app.c: they format text, they do not change the app.
#include "app.h"
#include "app_status.h"
#include "app_monitor.h"
#include "app_nav.h"
#include "haptic.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "app_report";

static bool line_ok(char *buf, size_t cap, int written)
{
    if (written >= 0 && (size_t)written < cap) return true;
    ESP_LOGE(TAG, "report line needs %d bytes, buffer has %u", written + 1, (unsigned)cap);
    if (cap) buf[0] = '\0';
    return false;
}

bool app_state_line(char *buf, size_t cap)
{
    static const char *const SCREEN[NAV_COUNT] = { "home", "events", "settings" };
    const char *cal = monitor_is_learning() ? "learning" : (monitor_has_baseline() ? "ok" : "none");
    app_status_t st = app_status();
    return line_ok(buf, cap, snprintf(buf, cap, "screen=%s count=%u sens=%d cal=%s dim=%d listen=%d",
                                      SCREEN[nav_current()], st.count, st.sensitivity, cal,
                                      (int)st.dim, (int)!monitor_is_paused()));
}

bool app_mic_line(char *buf, size_t cap)
{
    int level, base, thr;
    monitor_levels(&level, &base, &thr);
    return line_ok(buf, cap, snprintf(buf, cap, "level=%.2f peak=%.2f base=%.2f thr=%.2f",
                                      level / 100.0, monitor_take_peak() / 100.0, base / 100.0,
                                      thr / 100.0));
}

bool app_selftest_line(char *buf, size_t cap)
{
    int quiet, clicked;
    monitor_selftest(haptic_click, &quiet, &clicked);
    return line_ok(buf, cap, snprintf(buf, cap, "quiet=%.2f click=%.2f delta=%.2f", quiet / 100.0,
                                      clicked / 100.0, (clicked - quiet) / 100.0));
}
