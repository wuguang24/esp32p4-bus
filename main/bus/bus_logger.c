/**
 * @file bus_logger.c
 * @brief 从 capture 增量写 CSV 到 /sdcard/bus_YYYYMMDD_HHMMSS.csv
 */

#include "bus_logger.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc.h"

#include "bus_capture.h"
#include "bus_frame.h"

static const char *TAG = "bus_logger";

static volatile bool s_recording;
static volatile bool s_task_stop;
static TaskHandle_t s_task;
static FILE *s_fp;
static char s_path[96];
static uint32_t s_lines;
static esp_err_t s_last_err;
static bus_logger_state_t s_state = BUS_LOGGER_IDLE;
static uint64_t s_seq;

static void bus_logger_hex(const uint8_t *data, size_t len, char *out, size_t out_sz)
{
    size_t pos = 0;
    out[0] = '\0';
    for (size_t i = 0; i < len && (pos + 3) < out_sz; ++i) {
        int n = snprintf(out + pos, out_sz - pos, "%02X", data[i]);
        if (n < 0) {
            break;
        }
        pos += (size_t)n;
        if (i + 1 < len && (pos + 1) < out_sz) {
            out[pos++] = ' ';
            out[pos] = '\0';
        }
    }
}

static bool bus_logger_make_path(char *path, size_t path_sz)
{
    time_t now = 0;
    time(&now);
    struct tm tmv;
    bool have_rtc = (now > 1700000000); /* 粗判是否已校时 */

    if (have_rtc && localtime_r(&now, &tmv) != NULL) {
        snprintf(path, path_sz, MOUNT_POINT "/bus_%04d%02d%02d_%02d%02d%02d.csv",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    } else {
        uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(path, path_sz, MOUNT_POINT "/bus_%lu.csv", (unsigned long)ms);
    }
    return true;
}

static void bus_logger_write_header(FILE *fp)
{
    fputs("t_us,t_ms,src,dir,id,dlc,flags,hexdata\n", fp);
    fflush(fp);
}

static void bus_logger_write_frame(FILE *fp, const bus_frame_t *f)
{
    char hex[64 * 3 + 4];
    bus_logger_hex(f->data, f->len, hex, sizeof(hex));
    fprintf(fp, "%llu,%lu,%u,%u,0x%lX,%u,0x%02X,%s\n",
            (unsigned long long)f->t_us,
            (unsigned long)f->t_ms,
            (unsigned)f->src,
            (unsigned)f->dir,
            (unsigned long)f->id,
            (unsigned)f->dlc,
            (unsigned)f->flags,
            hex);
}

static void bus_logger_task(void *arg)
{
    (void)arg;
    bus_frame_t batch[32];

    while (!s_task_stop && s_recording && s_fp != NULL) {
        size_t n = bus_capture_copy_since(&s_seq, batch, 32);
        if (n == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        for (size_t i = 0; i < n; ++i) {
            bus_logger_write_frame(s_fp, &batch[i]);
            s_lines++;
        }
        if (fflush(s_fp) != 0) {
            s_last_err = ESP_FAIL;
            s_state = BUS_LOGGER_ERROR_IO;
            ESP_LOGE(TAG, "fflush failed");
            break;
        }
    }

    if (s_fp) {
        fflush(s_fp);
        fclose(s_fp);
        s_fp = NULL;
    }
    s_recording = false;
    if (s_state == BUS_LOGGER_RECORDING) {
        s_state = BUS_LOGGER_IDLE;
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t bus_logger_start(void)
{
    if (s_recording) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 尝试挂载；无卡则返回错误但不崩溃 */
    esp_err_t err = sdmmc_init();
    if (err != ESP_OK || sdmmc_mount_flag != 0x01) {
        s_last_err = (err != ESP_OK) ? err : ESP_ERR_NOT_FOUND;
        s_state = BUS_LOGGER_ERROR_NO_SD;
        ESP_LOGW(TAG, "SD not ready (%s), skip recording", esp_err_to_name(s_last_err));
        return s_last_err;
    }

    bus_logger_make_path(s_path, sizeof(s_path));
    s_fp = fopen(s_path, "w");
    if (s_fp == NULL) {
        s_last_err = ESP_FAIL;
        s_state = BUS_LOGGER_ERROR_IO;
        ESP_LOGE(TAG, "fopen %s failed", s_path);
        return ESP_FAIL;
    }

    bus_logger_write_header(s_fp);
    s_lines = 0;
    s_seq = 0;
    {
        bus_capture_stats_t st;
        bus_capture_get_stats(&st);
        s_seq = st.total_pushed; /* 从当前点增量，避免把历史一次性刷盘 */
    }
    s_task_stop = false;
    s_recording = true;
    s_state = BUS_LOGGER_RECORDING;
    s_last_err = ESP_OK;

    if (xTaskCreate(bus_logger_task, "bus_logger", 4096, NULL, 3, &s_task) != pdPASS) {
        fclose(s_fp);
        s_fp = NULL;
        s_recording = false;
        s_state = BUS_LOGGER_ERROR_IO;
        s_last_err = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "recording -> %s", s_path);
    return ESP_OK;
}

esp_err_t bus_logger_stop(void)
{
    if (!s_recording && s_task == NULL) {
        return ESP_OK;
    }
    s_task_stop = true;
    for (int i = 0; i < 100 && s_task != NULL; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    /* 任务内会 fclose；若任务未起则这里兜底 */
    if (s_fp) {
        fflush(s_fp);
        fclose(s_fp);
        s_fp = NULL;
    }
    s_recording = false;
    if (s_state == BUS_LOGGER_RECORDING) {
        s_state = BUS_LOGGER_IDLE;
    }
    ESP_LOGI(TAG, "stopped, lines=%lu", (unsigned long)s_lines);
    return ESP_OK;
}

bool bus_logger_is_recording(void)
{
    return s_recording;
}

void bus_logger_get_status(bus_logger_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->state = s_state;
    out->recording = s_recording;
    out->lines_written = s_lines;
    out->last_err = s_last_err;
    snprintf(out->path, sizeof(out->path), "%s", s_path);
}
