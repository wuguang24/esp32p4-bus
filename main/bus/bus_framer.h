/**
 * @file bus_framer.h
 * @brief UART/RS485 帧切分：空闲超时 / 定长 / 帧头+帧尾
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bus_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUS_FR_IDLE = 0, /**< 空闲超时分包（驱动 RX timeout） */
    BUS_FR_FIXED,    /**< 定长帧 */
    BUS_FR_MARK,     /**< 帧头 + 帧尾 */
} bus_framer_mode_t;

typedef struct {
    bus_framer_mode_t mode;
    uint8_t fixed_len; /**< 1..BUS_*_FRAME_MAX，定长模式 */
    uint8_t sof[4];
    uint8_t sof_n; /**< 0..4，MARK 模式帧头字节数；0 表示无头 */
    uint8_t eof[4];
    uint8_t eof_n; /**< 0..4，MARK 模式帧尾；0 表示无尾（仅头+定长/满缓冲） */
} bus_framer_cfg_t;

typedef struct {
    bus_framer_cfg_t cfg;
    uint8_t buf[BUS_UART_FRAME_MAX];
    size_t len;
    bool in_frame;
    uint8_t sof_match;
} bus_framer_t;

typedef void (*bus_framer_emit_fn)(const uint8_t *data, size_t len, void *user);

void bus_framer_init(bus_framer_t *fr, const bus_framer_cfg_t *cfg);
void bus_framer_reset(bus_framer_t *fr);
void bus_framer_set_cfg(bus_framer_t *fr, const bus_framer_cfg_t *cfg);
void bus_framer_get_cfg(const bus_framer_t *fr, bus_framer_cfg_t *out);

/** 喂入原始字节；完整帧通过 emit 回调吐出 */
void bus_framer_feed(bus_framer_t *fr, const uint8_t *data, size_t n,
                     bus_framer_emit_fn emit, void *user);

#ifdef __cplusplus
}
#endif
