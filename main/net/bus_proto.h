/**
 * @file bus_proto.h
 * @brief BUS1 WiFi UDP 协议（端口 9527）
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUS1_MAGIC              0x31535542u  /* 'BUS1' LE */
#define BUS1_UDP_PORT           9527
#define BUS1_PAIR_TOKEN         "bus1-pair-2026"
#define BUS1_PAIR_TOKEN_LEN     16

typedef enum {
    BUS1_TYPE_TELEM = 1,
    BUS1_TYPE_FRAME = 2,
    BUS1_TYPE_CMD   = 3,
    BUS1_TYPE_ACK   = 4,
    BUS1_TYPE_HELLO = 5,
} bus1_type_t;

typedef enum {
    BUS1_CMD_CLEAR          = 1,
    BUS1_CMD_SET_ALLOW_TX   = 2,
    BUS1_CMD_SET_CAN_BAUD   = 3,
    BUS1_CMD_SET_RS485_BAUD = 4,
    BUS1_CMD_SET_UART_BAUD  = 5,
    BUS1_CMD_LOGGER_START   = 6,
    BUS1_CMD_LOGGER_STOP    = 7,
    BUS1_CMD_CAN_SEND       = 8,
    BUS1_CMD_RS485_SEND     = 9,
    BUS1_CMD_UART_SEND      = 10,
    BUS1_CMD_I2C_SCAN       = 11,
    BUS1_CMD_SET_LISTEN     = 12,
} bus1_cmd_id_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  type;
    uint8_t  flags;
    uint16_t seq;
    uint32_t tick_ms;
    uint16_t len;
} bus1_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t rx_count;
    uint32_t tx_count;
    uint32_t err_count;
    uint32_t drop_count;
    float    rx_rate;
    uint8_t  can_on;
    uint8_t  rs485_on;
    uint8_t  uart_on;
    uint8_t  i2c_on;
    uint8_t  spi_on;
    uint8_t  recording;
    uint8_t  allow_tx;
    uint8_t  listen_only;
    uint32_t can_baud;
    uint32_t rs485_baud;
    uint32_t uart_baud;
} bus1_telem_t;

typedef struct __attribute__((packed)) {
    uint32_t t_ms;
    uint8_t  src;
    uint8_t  dir;
    uint8_t  flags;
    uint8_t  len;
    uint32_t id;
    uint8_t  data[64];
} bus1_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t cmd_id;
    uint8_t b0;
    uint16_t reserved;
    uint32_t u0;
    uint32_t u1;
    uint8_t  data[64];
    uint8_t  dlen;
    uint8_t  pad[3];
} bus1_cmd_t;

typedef struct __attribute__((packed)) {
    uint8_t cmd_id;
    uint8_t result;
    uint16_t reserved;
} bus1_ack_t;

typedef struct __attribute__((packed)) {
    char token[16];
} bus1_hello_t;

#ifdef __cplusplus
}
#endif
