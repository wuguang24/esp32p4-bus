/**
 * @file bus_adc.c
 * @brief ADC1 oneshot（参考慧勤 basic_routines/10_adc）
 */

#include "bus_adc.h"

#include <string.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "soc/adc_channel.h"

static const char *TAG = "bus_adc";

#define ADC_UNIT_X      ADC_UNIT_1
#define ADC_CHAN        ADC_CHANNEL_4   /* GPIO20 */
#define ADC_ATTEN       ADC_ATTEN_DB_12
#define ADC_BITWIDTH    ADC_BITWIDTH_12

static adc_oneshot_unit_handle_t s_unit;
static adc_cali_handle_t s_cali;
static bool s_calibrated;
static bool s_inited;

static uint16_t s_hist[BUS_ADC_HIST_LEN];
static size_t s_hist_head;
static size_t s_hist_count;

static bool calibration_init(void)
{
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id = ADC_UNIT_X,
        .chan = ADC_CHAN,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cfg, &s_cali);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "curve fitting OK");
        return true;
    }
    ESP_LOGW(TAG, "calibration skip: %s", esp_err_to_name(ret));
    s_cali = NULL;
    return false;
}

esp_err_t bus_adc_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t ucfg = {
        .unit_id = ADC_UNIT_X,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&ucfg, &s_unit);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t ccfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    err = adc_oneshot_config_channel(s_unit, ADC_CHAN, &ccfg);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(s_unit);
        s_unit = NULL;
        return err;
    }

    s_calibrated = calibration_init();
    s_inited = true;
    ESP_LOGI(TAG, "ADC GPIO%d (ADC1_CH4) ready", (int)BUS_ADC_GPIO);
    return ESP_OK;
}

static int raw_to_mv(int raw)
{
    if (s_calibrated && s_cali) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) {
            return mv;
        }
    }
    /* 12bit / 12dB ≈ 0~3.3V 粗估 */
    return (raw * 3300) / 4095;
}

esp_err_t bus_adc_read(bus_adc_sample_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_inited) {
        esp_err_t err = bus_adc_init();
        if (err != ESP_OK) {
            return err;
        }
    }
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_unit, ADC_CHAN, &raw);
    if (err != ESP_OK) {
        return err;
    }
    out->raw = raw;
    out->mv = raw_to_mv(raw);
    out->calibrated = s_calibrated;
    out->gpio = BUS_ADC_GPIO;
    return ESP_OK;
}

esp_err_t bus_adc_read_avg(bus_adc_sample_t *out, uint32_t times)
{
    if (!out || times < 3) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_inited) {
        esp_err_t err = bus_adc_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    int buf[16];
    if (times > 16) {
        times = 16;
    }
    for (uint32_t i = 0; i < times; i++) {
        esp_err_t err = adc_oneshot_read(s_unit, ADC_CHAN, &buf[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    /* 升序 */
    for (uint32_t i = 0; i + 1 < times; i++) {
        for (uint32_t j = i + 1; j < times; j++) {
            if (buf[i] > buf[j]) {
                int t = buf[i];
                buf[i] = buf[j];
                buf[j] = t;
            }
        }
    }

    uint32_t sum = 0;
    uint32_t n = times - 2;
    for (uint32_t i = 1; i + 1 < times; i++) {
        sum += (uint32_t)buf[i];
    }
    int avg = (int)(sum / n);
    out->raw = avg;
    out->mv = raw_to_mv(avg);
    out->calibrated = s_calibrated;
    out->gpio = BUS_ADC_GPIO;
    return ESP_OK;
}

void bus_adc_hist_push(int mv)
{
    if (mv < 0) {
        mv = 0;
    }
    if (mv > 3300) {
        mv = 3300;
    }
    s_hist[s_hist_head] = (uint16_t)mv;
    s_hist_head = (s_hist_head + 1) % BUS_ADC_HIST_LEN;
    if (s_hist_count < BUS_ADC_HIST_LEN) {
        s_hist_count++;
    }
}

size_t bus_adc_get_hist(uint16_t *out, size_t max_n)
{
    if (!out || max_n == 0) {
        return 0;
    }
    size_t n = s_hist_count < max_n ? s_hist_count : max_n;
    size_t start = (s_hist_head + BUS_ADC_HIST_LEN - n) % BUS_ADC_HIST_LEN;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_hist[(start + i) % BUS_ADC_HIST_LEN];
    }
    return n;
}

gpio_num_t bus_adc_get_gpio(void)
{
    return BUS_ADC_GPIO;
}
