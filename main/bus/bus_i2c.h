#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "bus_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bus_i2c_start(uint32_t freq_hz);
esp_err_t bus_i2c_stop(void);
bool bus_i2c_is_running(void);
uint32_t bus_i2c_get_freq(void);

/** 扫描 0x08~0x77，found[] 写入地址，返回发现数量 */
int bus_i2c_scan(uint8_t *found, int max_n);

esp_err_t bus_i2c_write(uint8_t addr7, const uint8_t *data, size_t len);
esp_err_t bus_i2c_read(uint8_t addr7, uint8_t *data, size_t len);
esp_err_t bus_i2c_write_read(uint8_t addr7, const uint8_t *wr, size_t wr_len,
                             uint8_t *rd, size_t rd_len);

#ifdef __cplusplus
}
#endif
