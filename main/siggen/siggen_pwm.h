/**
 * @file siggen_pwm.h
 * @brief 可调频 PWM（LEDC），默认 GPIO11
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SIGGEN_PWM_GPIO_DEFAULT     GPIO_NUM_11
#define SIGGEN_PWM_FREQ_MIN_HZ      100u
#define SIGGEN_PWM_FREQ_MAX_HZ      40000u
#define SIGGEN_PWM_DUTY_DEFAULT     50u

esp_err_t siggen_pwm_init(void);
esp_err_t siggen_pwm_start(void);
esp_err_t siggen_pwm_stop(void);
bool siggen_pwm_is_running(void);

esp_err_t siggen_pwm_set_freq(uint32_t freq_hz);
esp_err_t siggen_pwm_set_duty(uint8_t duty_pct); /* 0~100 */

uint32_t siggen_pwm_get_freq(void);
uint8_t siggen_pwm_get_duty(void);
gpio_num_t siggen_pwm_get_gpio(void);

#ifdef __cplusplus
}
#endif
