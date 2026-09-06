#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bus_spi_start(uint32_t freq_hz, uint8_t mode);
esp_err_t bus_spi_stop(void);
bool bus_spi_is_running(void);
uint32_t bus_spi_get_freq(void);
uint8_t bus_spi_get_mode(void);

/** 全双工传输；tx/rx 可为 NULL；结果写入 capture */
esp_err_t bus_spi_xfer(const uint8_t *tx, uint8_t *rx, size_t len);

#ifdef __cplusplus
}
#endif
