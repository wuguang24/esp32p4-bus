/**
 * @file bus_frame.h
 * @brief 统一捕获帧（CAN / RS485 / UART / I2C / SPI）
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUS_SRC_CAN = 0,
    BUS_SRC_RS485 = 1,
    BUS_SRC_UART = 2,
    BUS_SRC_I2C = 3,
    BUS_SRC_SPI = 4,
} bus_src_t;

#define BUS_DIR_RX              0
#define BUS_DIR_TX              1

#define BUS_FLAG_EXT            (1u << 0)
#define BUS_FLAG_RTR            (1u << 1)
#define BUS_FLAG_ERR            (1u << 2)
#define BUS_FLAG_CRC_OK         (1u << 3)
#define BUS_FLAG_NACK           (1u << 4)
#define BUS_FLAG_CRC_BAD        (1u << 5)

typedef struct {
    uint64_t t_us;     /* 单调时钟 µs（主时间基） */
    uint32_t t_ms;     /* 兼容旧日志/UDP：t_us/1000 */
    uint8_t src;
    uint8_t dir;
    uint8_t flags;
    uint8_t dlc;       /* CAN DLC；其它总线可与 len 相同 */
    uint32_t id;       /* CAN id / I2C addr / Modbus 首字节提示 */
    uint8_t data[64];
    uint8_t len;
} bus_frame_t;

/** 填入当前时间戳（t_us + t_ms） */
void bus_frame_stamp(bus_frame_t *f);

#ifdef __cplusplus
}
#endif
