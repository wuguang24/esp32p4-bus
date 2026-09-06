/**
 * @file bus_rs485.h
 * @brief RS485 驱动（UART1 + GPIO32 DE/RE，默认接收态）
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "bus_frame.h"
#include "bus_framer.h"
#include "bus_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bus_rs485_frame_cb_t)(const bus_frame_t *frame, void *user);

/**
 * @brief 启动 RS485
 * @param baud    波特率，上限 BUS_RS485_BAUD_MAX(250000)
 * @param parity  校验
 */
esp_err_t bus_rs485_start(uint32_t baud, bus_rs485_parity_t parity);

esp_err_t bus_rs485_stop(void);

bool bus_rs485_is_running(void);
uint32_t bus_rs485_get_baud(void);
bus_rs485_parity_t bus_rs485_get_parity(void);

void bus_rs485_set_allow_tx(bool allow);
bool bus_rs485_get_allow_tx(void);

/**
 * @brief 发送：需 allow_tx；DE=1 → write → wait_tx_done → DE=0
 */
esp_err_t bus_rs485_send(const uint8_t *buf, size_t len);

void bus_rs485_set_rx_cb(bus_rs485_frame_cb_t cb, void *user);
void bus_rs485_set_framer(const bus_framer_cfg_t *cfg);
void bus_rs485_get_framer(bus_framer_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
