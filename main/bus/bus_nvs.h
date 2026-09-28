/**
 * @file bus_nvs.h
 * @brief 总线参数 NVS 持久化（波特率/校验/只听/滤波/帧规）
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bus_framer.h"
#include "bus_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t can_baud;
    uint32_t rs485_baud;
    uint32_t uart_baud;
    uint32_t i2c_hz;
    uint32_t spi_hz;
    uint8_t spi_mode;
    bus_rs485_parity_t rs485_parity;
    bus_uart_parity_t uart_parity;
    bus_uart_stop_t uart_stop;
    bool listen_only;
    bool allow_tx;

    bool can_filt_en;
    bool can_filt_ext;
    uint32_t can_filt_id;
    uint32_t can_filt_mask;

    bus_framer_cfg_t uart_fr;
    bus_framer_cfg_t rs485_fr;
} bus_nvs_cfg_t;

void bus_nvs_defaults(bus_nvs_cfg_t *out);
bool bus_nvs_load(bus_nvs_cfg_t *out);
bool bus_nvs_save(const bus_nvs_cfg_t *in);
bool bus_nvs_erase(void);

#ifdef __cplusplus
}
#endif
