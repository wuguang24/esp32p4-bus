/**
 * @file board_io_reserve.h
 * @brief 总线分析仪引脚一览（与 bus/bus_pins.h 一致）
 */

#pragma once

#include "driver/gpio.h"

/* CAN TJA1050 */
#define BOARD_CAN_TX_GPIO       GPIO_NUM_33
#define BOARD_CAN_RX_GPIO       GPIO_NUM_34

/* RS485 TP8485E-SR */
#define BOARD_RS485_RX_GPIO     GPIO_NUM_30
#define BOARD_RS485_TX_GPIO     GPIO_NUM_31
#define BOARD_RS485_DE_GPIO     GPIO_NUM_32

/* TTL UART2 */
#define BOARD_UART_TX_GPIO      GPIO_NUM_9
#define BOARD_UART_RX_GPIO      GPIO_NUM_10

/* I2C1 */
#define BOARD_I2C_SDA_GPIO      GPIO_NUM_7
#define BOARD_I2C_SCL_GPIO      GPIO_NUM_8

/* SPI（CS 用 12，腾出 20 给 ADC） */
#define BOARD_SPI_SCLK_GPIO     GPIO_NUM_26
#define BOARD_SPI_MOSI_GPIO     GPIO_NUM_27
#define BOARD_SPI_MISO_GPIO     GPIO_NUM_6
#define BOARD_SPI_CS_GPIO       GPIO_NUM_12

/* PWM / ADC */
#define BOARD_PWM_GPIO          GPIO_NUM_11
#define BOARD_ADC_GPIO          GPIO_NUM_20
