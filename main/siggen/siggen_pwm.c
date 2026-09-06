/**
 * @file siggen_pwm.c
 * @brief LEDC 可调频 PWM（参考慧勤 08_ledc）
 */

#include "siggen_pwm.h"

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "siggen_pwm";

#define PWM_TIMER       LEDC_TIMER_0
#define PWM_CHANNEL     LEDC_CHANNEL_0
#define PWM_MODE        LEDC_LOW_SPEED_MODE
#define PWM_RES         LEDC_TIMER_10_BIT
#define PWM_DUTY_MAX    ((1u << 10) - 1u)

static bool s_inited;
static bool s_running;
static uint32_t s_freq = 1000;
static uint8_t s_duty = SIGGEN_PWM_DUTY_DEFAULT;
static gpio_num_t s_gpio = SIGGEN_PWM_GPIO_DEFAULT;

static uint32_t duty_to_ticks(uint8_t pct)
{
    if (pct > 100) {
        pct = 100;
    }
    return (PWM_DUTY_MAX * (uint32_t)pct) / 100u;
}

static esp_err_t apply_duty(void)
{
    esp_err_t err = ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty_to_ticks(s_duty));
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(PWM_MODE, PWM_CHANNEL);
}

esp_err_t siggen_pwm_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    ledc_timer_config_t timer = {
        .speed_mode = PWM_MODE,
        .duty_resolution = PWM_RES,
        .timer_num = PWM_TIMER,
        .freq_hz = s_freq,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }

    ledc_channel_config_t ch = {
        .speed_mode = PWM_MODE,
        .channel = PWM_CHANNEL,
        .timer_sel = PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = s_gpio,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) {
        return err;
    }

    /* 默认停在低电平，等 start */
    ledc_stop(PWM_MODE, PWM_CHANNEL, 0);
    s_inited = true;
    s_running = false;
    ESP_LOGI(TAG, "PWM ready GPIO%d", (int)s_gpio);
    return ESP_OK;
}

esp_err_t siggen_pwm_start(void)
{
    if (!s_inited) {
        esp_err_t err = siggen_pwm_init();
        if (err != ESP_OK) {
            return err;
        }
    }
    esp_err_t err = ledc_set_freq(PWM_MODE, PWM_TIMER, s_freq);
    if (err != ESP_OK) {
        return err;
    }
    err = apply_duty();
    if (err != ESP_OK) {
        return err;
    }
    s_running = true;
    ESP_LOGI(TAG, "PWM ON %lu Hz duty %u%%", (unsigned long)s_freq, (unsigned)s_duty);
    return ESP_OK;
}

esp_err_t siggen_pwm_stop(void)
{
    if (!s_inited) {
        return ESP_OK;
    }
    esp_err_t err = ledc_stop(PWM_MODE, PWM_CHANNEL, 0);
    s_running = false;
    ESP_LOGI(TAG, "PWM OFF");
    return err;
}

bool siggen_pwm_is_running(void)
{
    return s_running;
}

esp_err_t siggen_pwm_set_freq(uint32_t freq_hz)
{
    if (freq_hz < SIGGEN_PWM_FREQ_MIN_HZ) {
        freq_hz = SIGGEN_PWM_FREQ_MIN_HZ;
    }
    if (freq_hz > SIGGEN_PWM_FREQ_MAX_HZ) {
        freq_hz = SIGGEN_PWM_FREQ_MAX_HZ;
    }
    s_freq = freq_hz;
    if (!s_inited || !s_running) {
        return ESP_OK;
    }
    return ledc_set_freq(PWM_MODE, PWM_TIMER, s_freq);
}

esp_err_t siggen_pwm_set_duty(uint8_t duty_pct)
{
    if (duty_pct > 100) {
        duty_pct = 100;
    }
    s_duty = duty_pct;
    if (!s_inited || !s_running) {
        return ESP_OK;
    }
    return apply_duty();
}

uint32_t siggen_pwm_get_freq(void)
{
    return s_freq;
}

uint8_t siggen_pwm_get_duty(void)
{
    return s_duty;
}

gpio_num_t siggen_pwm_get_gpio(void)
{
    return s_gpio;
}
