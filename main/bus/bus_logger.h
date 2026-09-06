/**
 * @file bus_logger.h
 * @brief SD 卡 CSV 落盘
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUS_LOGGER_IDLE = 0,
    BUS_LOGGER_RECORDING,
    BUS_LOGGER_ERROR_NO_SD,
    BUS_LOGGER_ERROR_IO,
} bus_logger_state_t;

typedef struct {
    bus_logger_state_t state;
    bool recording;
    char path[96];
    uint32_t lines_written;
    esp_err_t last_err;
} bus_logger_status_t;

/**
 * @brief 开始录制：依赖 sdmmc_init / MOUNT_POINT "/sdcard"
 * @note SD 未插入时返回错误，不崩溃
 */
esp_err_t bus_logger_start(void);

esp_err_t bus_logger_stop(void);
bool bus_logger_is_recording(void);
void bus_logger_get_status(bus_logger_status_t *out);

#ifdef __cplusplus
}
#endif
