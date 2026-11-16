// settings.c — NVS-backed values with a debounced writer task (flash writes stall for
// milliseconds; they must not run on the LVGL task or the mic task).
#include "settings.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdint.h>

static const char *TAG = "settings";

#define NVS_NAMESPACE   "sound"
#define KEY_BASELINE    "baseline"
#define KEY_SENS        "sens"
#define DEBOUNCE_MS     1000u
#define TASK_STACK      4096u

static atomic_bool s_ready, s_has_baseline, s_has_sens, s_dirty_baseline, s_dirty_sens;
static atomic_int  s_baseline, s_sens;
static TaskHandle_t s_task;

static void load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {   // ESP_ERR_NVS_NOT_FOUND on a fresh device: nothing stored yet
        ESP_LOGI(TAG, "nothing stored yet (%s)", esp_err_to_name(err));
        return;
    }
    int32_t v;
    if (nvs_get_i32(h, KEY_BASELINE, &v) == ESP_OK) { s_baseline = v; s_has_baseline = true; }
    if (nvs_get_i32(h, KEY_SENS, &v) == ESP_OK)     { s_sens = v;     s_has_sens = true; }
    nvs_close(h);
}

static esp_err_t write_values(bool baseline, bool sens)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (baseline) err = nvs_set_i32(h, KEY_BASELINE, atomic_load(&s_baseline));
    if (sens && err == ESP_OK) err = nvs_set_i32(h, KEY_SENS, atomic_load(&s_sens));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static void write_dirty(void)
{
    bool baseline = atomic_exchange(&s_dirty_baseline, false);
    bool sens     = atomic_exchange(&s_dirty_sens, false);
    if (!baseline && !sens) return;

    esp_err_t err = write_values(baseline, sens);
    if (err != ESP_OK) {   // keep it pending: the next change retries
        if (baseline) s_dirty_baseline = true;
        if (sens)     s_dirty_sens = true;
        ESP_LOGE(TAG, "write failed, value kept in RAM only for now: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "saved (baseline=%d sens=%d); writer stack free %u of %u",
             atomic_load(&s_baseline), atomic_load(&s_sens),
             (unsigned)uxTaskGetStackHighWaterMark(NULL), TASK_STACK);
}

static void writer_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // Wait until the value has been left alone for DEBOUNCE_MS, then write it once.
        while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DEBOUNCE_MS)) != 0) { }
        write_dirty();
    }
}

void settings_init(void)
{
    load();
    if (xTaskCreate(writer_task, "settings", TASK_STACK, NULL, 2, &s_task) != pdPASS)
        ESP_LOGE(TAG, "writer task not created: changes will not be saved");
    s_ready = true;
    ESP_LOGI(TAG, "loaded: baseline %s (%d), sensitivity %s (%d)",
             s_has_baseline ? "yes" : "none", atomic_load(&s_baseline),
             s_has_sens ? "yes" : "none", atomic_load(&s_sens));
}

settings_t settings_get(void)
{
    if (!s_ready) {
        ESP_LOGE(TAG, "settings_get() before settings_init()");
        return (settings_t){0};
    }
    return (settings_t){
        .has_baseline = s_has_baseline, .baseline_cdb = atomic_load(&s_baseline),
        .has_sensitivity = s_has_sens,  .sensitivity = atomic_load(&s_sens),
    };
}

static void request_write(void)
{
    if (!s_task) { ESP_LOGE(TAG, "no writer task: value not saved"); return; }
    xTaskNotifyGive(s_task);
}

void settings_set_baseline(int baseline_cdb)
{
    s_baseline = baseline_cdb;
    s_has_baseline = true;
    s_dirty_baseline = true;
    request_write();
}

void settings_set_sensitivity(int sensitivity)
{
    s_sens = sensitivity;
    s_has_sens = true;
    s_dirty_sens = true;
    request_write();
}
