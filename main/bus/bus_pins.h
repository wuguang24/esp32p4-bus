/**
 * @file bus_pins.h
 * @brief 五总线引脚与速率（分析仪定稿）
 *
 * CAN   TJA1050     TX33 RX34           40k~1Mbps
 * RS485 TP8485E-SR  RX30 TX31 DE/RE32   ≤250kbps
 * UART2 TTL         TX9  RX10
 * I2C1              SDA7 SCL8  (INT可选11)
 * SPI               SCLK26 MOSI27 MISO6 CS12
 * PWM               GPIO11（LEDC）
 * ADC               GPIO20 = ADC1_CH4（P4 仅 16~23 可 ADC；20 已从 SPI CS 腾出）
 *
 * 已移除片内以太网与 FOC；触摸 I2C0(16/17) / SD / WiFi 勿动。
 */

#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* —— CAN (TJA1050 + TWAI) —— */
#define BUS_CAN_TX_GPIO             GPIO_NUM_33
#define BUS_CAN_RX_GPIO             GPIO_NUM_34
#define BUS_CAN_BAUD_MIN            40000u
#define BUS_CAN_BAUD_MAX            1000000u
#define BUS_CAN_BAUD_DEFAULT        500000u
#define BUS_CAN_RX_QUEUE_LEN        64

typedef enum {
    BUS_CAN_RATE_50K = 0,
    BUS_CAN_RATE_100K,
    BUS_CAN_RATE_125K,
    BUS_CAN_RATE_250K,
    BUS_CAN_RATE_500K,
    BUS_CAN_RATE_1M,
    BUS_CAN_RATE_COUNT
} bus_can_rate_t;

/* —— RS485 (TP8485E-SR + UART1) —— */
#define BUS_RS485_UART_NUM          UART_NUM_1
#define BUS_RS485_TX_GPIO           GPIO_NUM_31
#define BUS_RS485_RX_GPIO           GPIO_NUM_30
#define BUS_RS485_DE_RE_GPIO        GPIO_NUM_32
#define BUS_RS485_BAUD_MIN          300u
#define BUS_RS485_BAUD_MAX          250000u
#define BUS_RS485_BAUD_DEFAULT      115200u
#define BUS_RS485_RX_BUF_SIZE       1024
#define BUS_RS485_TX_BUF_SIZE       512
#define BUS_RS485_FRAME_MAX         64
#define BUS_RS485_RX_TIMEOUT_SYM    3

typedef enum {
    BUS_RS485_RATE_9600 = 0,
    BUS_RS485_RATE_19200,
    BUS_RS485_RATE_38400,
    BUS_RS485_RATE_57600,
    BUS_RS485_RATE_115200,
    BUS_RS485_RATE_230400,
    BUS_RS485_RATE_COUNT
} bus_rs485_rate_t;

typedef enum {
    BUS_RS485_PARITY_NONE = 0,
    BUS_RS485_PARITY_EVEN,
    BUS_RS485_PARITY_ODD,
} bus_rs485_parity_t;

/* —— TTL UART2 —— */
#define BUS_UART_NUM                UART_NUM_2
#define BUS_UART_TX_GPIO            GPIO_NUM_9
#define BUS_UART_RX_GPIO            GPIO_NUM_10
#define BUS_UART_BAUD_DEFAULT       115200u
#define BUS_UART_BAUD_MAX           921600u
#define BUS_UART_RX_BUF_SIZE        2048
#define BUS_UART_FRAME_MAX          64
#define BUS_UART_RX_TIMEOUT_SYM     3

typedef enum {
    BUS_UART_PARITY_NONE = 0,
    BUS_UART_PARITY_EVEN,
    BUS_UART_PARITY_ODD,
} bus_uart_parity_t;

typedef enum {
    BUS_UART_STOP_1 = 0,
    BUS_UART_STOP_2,
} bus_uart_stop_t;

/* —— I2C1 —— */
#define BUS_I2C_PORT                I2C_NUM_1
#define BUS_I2C_SDA_GPIO            GPIO_NUM_7
#define BUS_I2C_SCL_GPIO            GPIO_NUM_8
#define BUS_I2C_INT_GPIO            GPIO_NUM_11
#define BUS_I2C_FREQ_DEFAULT_HZ     100000
#define BUS_I2C_FREQ_FAST_HZ        400000

/* —— SPI —— */
#define BUS_SPI_HOST                SPI2_HOST
#define BUS_SPI_SCLK_GPIO           GPIO_NUM_26
#define BUS_SPI_MOSI_GPIO           GPIO_NUM_27
#define BUS_SPI_MISO_GPIO           GPIO_NUM_6
#define BUS_SPI_CS_GPIO             GPIO_NUM_12
#define BUS_SPI_FREQ_DEFAULT_HZ     1000000

static inline uint32_t bus_can_rate_to_baud(bus_can_rate_t r)
{
    switch (r) {
    case BUS_CAN_RATE_50K:  return 50000u;
    case BUS_CAN_RATE_100K: return 100000u;
    case BUS_CAN_RATE_125K: return 125000u;
    case BUS_CAN_RATE_250K: return 250000u;
    case BUS_CAN_RATE_500K: return 500000u;
    case BUS_CAN_RATE_1M:   return 1000000u;
    default:                return BUS_CAN_BAUD_DEFAULT;
    }
}

static inline uint32_t bus_rs485_rate_to_baud(bus_rs485_rate_t r)
{
    switch (r) {
    case BUS_RS485_RATE_9600:   return 9600u;
    case BUS_RS485_RATE_19200:  return 19200u;
    case BUS_RS485_RATE_38400:  return 38400u;
    case BUS_RS485_RATE_57600:  return 57600u;
    case BUS_RS485_RATE_115200: return 115200u;
    case BUS_RS485_RATE_230400: return 230400u;
    default:                    return BUS_RS485_BAUD_DEFAULT;
    }
}

#ifdef __cplusplus
}
#endif
