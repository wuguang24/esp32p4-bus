/**
 * @file bus_tools.h
 * @brief 借鉴 Bit Pirate 的实用工具：波特率探测 / I2C 识别 / SPI Flash
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "bus_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 常见候选波特率表长度 */
#define BUS_BAUD_CAND_MAX 12

/**
 * @brief 在 UART2 RX 上尝试常见波特率，选得分最高者并应用
 * @param out_baud 探测结果（可为 NULL）
 * @param sample_ms 每个候选采样时长，建议 80~200
 */
esp_err_t bus_tools_uart_autodetect(uint32_t *out_baud, int sample_ms);

/**
 * @brief CAN 常见波特率试探（只听模式）：有效帧多、位/填充/格式错少者胜出并应用
 * @param sample_ms 每个候选采样时长，建议 120~300（总线需有流量）
 */
esp_err_t bus_tools_can_autodetect(uint32_t *out_baud, int sample_ms);

/**
 * @brief RS485 常见波特率×校验试探，选得分最高者并应用（需线上有数据）
 */
esp_err_t bus_tools_rs485_autodetect(uint32_t *out_baud, bus_rs485_parity_t *out_parity,
                                     int sample_ms);

/**
 * @brief 扫描 I2C 并附加设备名猜测，写入文本缓冲
 * @return 发现设备数；负值为错误
 */
int bus_tools_i2c_identify(char *out, size_t out_sz);

/**
 * @brief 读 I2C EEPROM/寄存器块：写 reg（1 或 2 字节地址）再读
 * @param addr_bytes 寄存器地址宽度 1 或 2（大端）
 */
esp_err_t bus_tools_i2c_dump(uint8_t addr7, uint16_t reg, uint8_t addr_bytes,
                             uint8_t *buf, size_t len);

/** SPI Flash JEDEC ID（CMD 0x9F），写出 mfr/type/cap */
esp_err_t bus_tools_spi_jedec(uint8_t id[3], char *name_out, size_t name_sz);

/** SPI Flash 读（CMD 0x03 + 24bit 地址），最多 256 字节 */
esp_err_t bus_tools_spi_flash_read(uint32_t addr, uint8_t *buf, size_t len);

/** 已知 I2C 地址友好名；未知返回 NULL */
const char *bus_tools_i2c_name(uint8_t addr7);

#ifdef __cplusplus
}
#endif
