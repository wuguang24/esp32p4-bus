/**
 * @file bus_onewire.h
 * @brief 1-Wire bitbang（默认与 DIO 共用 GPIO35）
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bus_ow_init(gpio_num_t gpio);
gpio_num_t bus_ow_get_gpio(void);

/** 复位脉冲；true=有从机应答 */
bool bus_ow_reset(void);

esp_err_t bus_ow_write_byte(uint8_t b);
uint8_t bus_ow_read_byte(void);

/** 单设备 Read ROM (0x33)，写出 8 字节 */
esp_err_t bus_ow_read_rom(uint8_t rom[8]);

/** 简化搜索：最多填 max_n 个 ROM；返回发现数 */
int bus_ow_search(uint8_t roms[][8], int max_n);

#ifdef __cplusplus
}
#endif
