/**
 * @file bus_dio.h
 * @brief 数字 IO 探针（默认 GPIO35，以太网拆除后空闲脚）
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BUS_DIO_GPIO_DEFAULT
#define BUS_DIO_GPIO_DEFAULT GPIO_NUM_35
#endif

typedef enum {
    BUS_DIO_IN = 0,
    BUS_DIO_OUT,
    BUS_DIO_IN_PU,
    BUS_DIO_IN_PD,
} bus_dio_mode_t;

esp_err_t bus_dio_init(gpio_num_t gpio);
gpio_num_t bus_dio_get_gpio(void);
esp_err_t bus_dio_set_mode(bus_dio_mode_t mode);
bus_dio_mode_t bus_dio_get_mode(void);
esp_err_t bus_dio_write(bool level);
int bus_dio_read(void); /* 0/1，负值错误 */

#ifdef __cplusplus
}
#endif
