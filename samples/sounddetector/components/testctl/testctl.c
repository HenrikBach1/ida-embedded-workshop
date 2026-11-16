// testctl.c — reads lines from the USB Serial/JTAG driver on its own task and injects events into
// the app under the LVGL lock, so the app's real handlers run exactly as they do for hardware input.
#include "testctl.h"
#include "app.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mbedtls/base64.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "testctl";

#define CMD_LINE_MAX   64
#define LOCK_MS    500
// `snap` renders the active screen on THIS task. The events list (20 rows of nested widgets)
// overflowed the template's 4 KB here (stack-canary panic on the first snapshot of that screen);
// the low-water mark is logged after every snapshot so the next deeper screen shows up early.
#define TASK_STACK 12288
#define REPLY_MAX  96

// One report line from the app, as "> <line>". `report` returns false if it did not fit.
static void reply_line(bool (*report)(char *buf, size_t cap))
{
    char line[REPLY_MAX];
    if (report(line, sizeof(line))) printf("> %s\n", line);
    else                            printf("> error report line too long\n");
    fflush(stdout);
}

// Snapshot the active screen into a PSRAM buffer and stream it as base64 rows.
static void do_snap(void)
{
    static uint8_t *s_pixels;
    const lv_color_format_t cf = LV_COLOR_FORMAT_RGB565;
    // Everything that reads LVGL state happens under the lock, including "which screen".
    if (!ui_lock(LOCK_MS)) { printf("> error lvgl lock\n"); fflush(stdout); return; }
    lv_obj_t *scr = lv_screen_active();
    uint32_t w = lv_obj_get_width(scr), h = lv_obj_get_height(scr);
    ui_unlock();
    uint32_t stride = lv_draw_buf_width_to_stride(w, cf);
    size_t size = (size_t)stride * h;

    if (!s_pixels) s_pixels = heap_caps_aligned_alloc(64, size, MALLOC_CAP_SPIRAM);
    if (!s_pixels) { printf("> error no memory for snapshot\n"); fflush(stdout); return; }

    lv_draw_buf_t buf;
    lv_draw_buf_init(&buf, w, h, cf, stride, s_pixels, size);
    lv_draw_buf_set_flag(&buf, LV_IMAGE_FLAGS_MODIFIABLE);

    if (!ui_lock(LOCK_MS)) { printf("> error lvgl lock\n"); fflush(stdout); return; }
    scr = lv_screen_active();            // screens are never deleted, but it may have changed
    lv_result_t r = lv_snapshot_take_to_draw_buf(scr, cf, &buf);
    ui_unlock();
    if (r != LV_RESULT_OK) { printf("> error snapshot failed\n"); fflush(stdout); return; }

    // One base64 row per line, each written in ONE call so a log line from another task cannot
    // land inside it; logging is muted for the duration of the dump for the same reason.
    size_t b64_cap = ((stride + 2) / 3) * 4 + 1;
    unsigned char *row = malloc(2 + b64_cap + 2);
    if (!row) { printf("> error no memory for base64\n"); fflush(stdout); return; }
    esp_log_level_t saved = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_NONE);
    printf("SNAP BEGIN %u %u RGB565\n", (unsigned)w, (unsigned)h);
    fflush(stdout);
    for (uint32_t y = 0; y < h; y++) {
        size_t olen = 0;
        row[0] = 'S'; row[1] = ':';
        mbedtls_base64_encode(row + 2, b64_cap, &olen, s_pixels + (size_t)y * stride, w * 2);
        row[2 + olen] = '\n';
        fwrite(row, 1, 3 + olen, stdout);
    }
    printf("SNAP END\n");
    fflush(stdout);
    esp_log_level_set("*", saved);
    free(row);
    ESP_LOGI(TAG, "snap done; stack low-water mark %u bytes free of %u",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), TASK_STACK);
}

// Commands that inject an input: run under the LVGL lock, then answer with the state line.
static void inject_knob(int n)    { app_inject_knob(n); }
static void inject_loud(int n)    { app_inject_loud(n); }
static void inject_sens(int n)    { app_inject_sens(n); }
static void inject_clear(int n)   { (void)n; app_inject_clear(); }
static void inject_relearn(int n) { (void)n; app_inject_relearn(); }
static void inject_idle(int n)    { (void)n; app_inject_idle(); }
static void inject_listen(int n)  { app_inject_listen(n); }
static void inject_reset(int n)   { (void)n; app_inject_reset(); }

static const struct { const char *name; void (*run)(int arg); } INJECT[] = {
    { "knob", inject_knob }, { "loud", inject_loud }, { "sens", inject_sens },
    { "clear", inject_clear }, { "relearn", inject_relearn }, { "idle", inject_idle },
    { "listen", inject_listen }, { "reset", inject_reset },
};

// Commands that only report. They run WITHOUT the lock (selftest sleeps for a second).
static void report_pong(void)     { printf("> pong\n"); fflush(stdout); }
static void report_state(void)    { reply_line(app_state_line); }
static void report_mic(void)      { reply_line(app_mic_line); }
static void report_selftest(void) { reply_line(app_selftest_line); }

static const struct { const char *name; void (*run)(void); } REPORT[] = {
    { "ping", report_pong }, { "snap", do_snap }, { "state", report_state },
    { "mic", report_mic }, { "selftest", report_selftest },
};

static bool run_inject(const char *name, int arg)
{
    for (size_t i = 0; i < sizeof(INJECT) / sizeof(INJECT[0]); i++) {
        if (strcmp(name, INJECT[i].name)) continue;
        if (!ui_lock(LOCK_MS)) { printf("> error lvgl lock\n"); fflush(stdout); return true; }
        INJECT[i].run(arg);
        ui_unlock();
        report_state();
        return true;
    }
    return false;
}

static bool run_report(const char *name)
{
    for (size_t i = 0; i < sizeof(REPORT) / sizeof(REPORT[0]); i++) {
        if (strcmp(name, REPORT[i].name)) continue;
        REPORT[i].run();
        return true;
    }
    return false;
}

static void handle(char *line)
{
    while (*line == ' ') line++;
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = '\0';
    if (!n) return;

    // "<name>" or "<name> <integer>"
    char *arg = strchr(line, ' ');
    int value = 0;
    if (arg) { *arg++ = '\0'; value = (int)strtol(arg, NULL, 10); }

    if (run_report(line) || run_inject(line, value)) return;
    printf("> error unknown command '%s'\n", line);
    fflush(stdout);
}

static void testctl_task(void *arg)
{
    (void)arg;
    char line[CMD_LINE_MAX];
    size_t len = 0;
    bool overlong = false;
    uint8_t buf[32];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n') {
                line[len] = '\0';
                if (overlong) { printf("> error line longer than %d\n", CMD_LINE_MAX - 1); fflush(stdout); }
                else          handle(line);
                len = 0; overlong = false;
            }
            else if (len < CMD_LINE_MAX - 1) line[len++] = c;
            else overlong = true;   // drop the WHOLE line, not just its head
        }
    }
}

void testctl_init(void)
{
    usb_serial_jtag_driver_config_t cfg = { .rx_buffer_size = 1024, .tx_buffer_size = 4096 };
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "usb_serial_jtag driver: %s", esp_err_to_name(err)); return; }
    usb_serial_jtag_vfs_use_driver();   // stdout now goes through the driver too (blocking, ordered)
    xTaskCreate(testctl_task, "testctl", TASK_STACK, NULL, 3, NULL);
    ESP_LOGI(TAG, "serial test console up: knob <±N> | loud <1-5> | sens <1-10> | clear | relearn | idle | listen <0|1> | reset | state | mic | selftest | snap | ping");
}
