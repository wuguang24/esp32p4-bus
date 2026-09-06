/**
 * @file bus_adc.h
 * @brief 单通道 ADC 采样（参考慧勤 10_adc）
 *
 * ESP32-P4 ADC1 仅 GPIO16~23 可用。板载触摸/LCD/WiFi 占满其余脚，
 * 选用 GPIO20 = ADC1_CH4；SPI CS 已改到 GPIO12 避免冲突。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUS_ADC_GPIO            GPIO_NUM_20
#define BUS_ADC_HIST_LEN        80

typedef struct {
    int raw;
    int mv;                 /* 校准后毫伏；校准失败时为估算值 */
    bool calibrated;
    gpio_num_t gpio;
} bus_adc_sample_t;

esp_err_t bus_adc_init(void);
esp_err_t bus_adc_read(bus_adc_sample_t *out);
/** 多次采样去极值均值，times 建议 4~16 */
esp_err_t bus_adc_read_avg(bus_adc_sample_t *out, uint32_t times);

/** 推入历史（mv，钳位到 0~3300），供波形 */
void bus_adc_hist_push(int mv);
size_t bus_adc_get_hist(uint16_t *out, size_t max_n);

gpio_num_t bus_adc_get_gpio(void);

#ifdef __cplusplus
}
#endif
