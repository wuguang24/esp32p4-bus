/**
 * @file bus_tools.c
 */

#include "bus_tools.h"

#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bus_app.h"
#include "bus_can.h"
#include "bus_capture.h"
#include "bus_i2c.h"
#include "bus_pins.h"
#include "bus_rs485.h"
#include "bus_spi.h"
#include "bus_uart.h"
#include "driver/gpio.h"

static const char *TAG = "bus_tools";

static const uint32_t s_baud_cands[] = {
    1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600
};

typedef struct {
    uint8_t addr;
    const char *name;
} i2c_id_t;

static const i2c_id_t s_i2c_ids[] = {
    {0x0C, "MAG/AK"},
    {0x18, "LIS3DH"},
    {0x1E, "HMC5883"},
    {0x20, "PCF8574"},
    {0x27, "LCD/PCF8574"},
    {0x29, "VL53L0X"},
    {0x3C, "SSD1306"},
    {0x3D, "SSD1306"},
    {0x40, "INA219/PCA9685"},
    {0x44, "SHT3x"},
    {0x48, "ADS1115"},
    {0x50, "EEPROM"},
    {0x51, "EEPROM"},
    {0x52, "EEPROM/PN532"},
    {0x53, "EEPROM/ADXL"},
    {0x57, "EEPROM"},
    {0x5A, "MLX90614"},
    {0x68, "MPU6050/DS3231"},
    {0x69, "MPU"},
    {0x76, "BME280"},
    {0x77, "BMP/BME"},
};

const char *bus_tools_i2c_name(uint8_t addr7)
{
    for (size_t i = 0; i < sizeof(s_i2c_ids) / sizeof(s_i2c_ids[0]); i++) {
        if (s_i2c_ids[i].addr == addr7) {
            return s_i2c_ids[i].name;
        }
    }
    return NULL;
}

static int score_rx_bytes(const uint8_t *buf, int n)
{
    if (n <= 0) {
        return 0;
    }
    int printable = 0;
    int nul = 0;
    for (int i = 0; i < n; i++) {
        uint8_t b = buf[i];
        if (b == 0) {
            nul++;
        }
        if ((b >= 0x20 && b < 0x7F) || b == '\r' || b == '\n' || b == '\t') {
            printable++;
        }
    }
    /* 有数据且可打印比例高 → 优；全 0/噪声扣分 */
    int score = n * 2 + printable * 3 - nul * 2;
    if (n >= 4 && printable * 100 / n >= 70) {
        score += 50;
    }
    return score;
}

esp_err_t bus_tools_uart_autodetect(uint32_t *out_baud, int sample_ms)
{
    if (sample_ms < 40) {
        sample_ms = 40;
    }
    if (sample_ms > 500) {
        sample_ms = 500;
    }

    bus_uart_parity_t parity = bus_uart_get_parity();
    bus_uart_stop_t stop = bus_uart_get_stop();
    uint32_t old_baud = bus_uart_get_baud();
    bool was_run = bus_uart_is_running();

    if (was_run) {
        (void)bus_uart_stop();
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    int best_score = -1;
    uint32_t best_baud = old_baud ? old_baud : 115200;
    uint8_t buf[256];

    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (parity == BUS_UART_PARITY_EVEN) {
        cfg.parity = UART_PARITY_EVEN;
    } else if (parity == BUS_UART_PARITY_ODD) {
        cfg.parity = UART_PARITY_ODD;
    }
    if (stop == BUS_UART_STOP_2) {
        cfg.stop_bits = UART_STOP_BITS_2;
    }

    esp_err_t err = uart_driver_install(BUS_UART_NUM, 2048, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart install: %s", esp_err_to_name(err));
        if (was_run) {
            (void)bus_app_set_uart_baud(old_baud);
        }
        return err;
    }
    ESP_ERROR_CHECK(uart_set_pin(BUS_UART_NUM, BUS_UART_TX_GPIO, BUS_UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    const size_t ncand = sizeof(s_baud_cands) / sizeof(s_baud_cands[0]);
    for (size_t i = 0; i < ncand; i++) {
        cfg.baud_rate = (int)s_baud_cands[i];
        if (uart_param_config(BUS_UART_NUM, &cfg) != ESP_OK) {
            continue;
        }
        (void)uart_flush_input(BUS_UART_NUM);
        vTaskDelay(pdMS_TO_TICKS(sample_ms));
        int n = uart_read_bytes(BUS_UART_NUM, buf, sizeof(buf), 0);
        int sc = score_rx_bytes(buf, n);
        ESP_LOGI(TAG, "baud %lu score=%d n=%d", (unsigned long)s_baud_cands[i], sc, n);
        if (sc > best_score) {
            best_score = sc;
            best_baud = s_baud_cands[i];
        }
    }

    uart_driver_delete(BUS_UART_NUM);

    if (best_score <= 0) {
        ESP_LOGW(TAG, "no clear baud; keep %lu", (unsigned long)old_baud);
        best_baud = old_baud ? old_baud : 115200;
        err = ESP_ERR_NOT_FOUND;
    } else {
        err = ESP_OK;
    }

    if (out_baud) {
        *out_baud = best_baud;
    }
    /* 应用探测结果（parity/stop 仍用 bus_app 当前配置） */
    esp_err_t apply = bus_app_set_uart_baud(best_baud);
    if (apply != ESP_OK) {
        ESP_LOGW(TAG, "apply baud fail: %s", esp_err_to_name(apply));
    }
    ESP_LOGI(TAG, "autodetect -> %lu (score=%d)", (unsigned long)best_baud, best_score);
    return err;
}

static const uint32_t s_can_baud_cands[] = {
    50000, 100000, 125000, 250000, 500000, 1000000
};

esp_err_t bus_tools_can_autodetect(uint32_t *out_baud, int sample_ms)
{
    if (sample_ms < 80) {
        sample_ms = 80;
    }
    if (sample_ms > 500) {
        sample_ms = 500;
    }

    bus_app_status_t st0;
    bus_app_get_status(&st0);
    uint32_t old_baud = st0.can_baud ? st0.can_baud : BUS_CAN_BAUD_DEFAULT;
    bool old_listen = st0.listen_only;
    bool filt_en = st0.can.filter_en;
    uint32_t filt_id = st0.can.filter_id;
    uint32_t filt_mask = st0.can.filter_mask;
    bool filt_ext = st0.can.filter_ext;

    /* 探测全程只听 + 全收，避免发帧/滤波干扰 */
    (void)bus_app_set_listen(true);
    (void)bus_app_set_can_filter(false, 0, 0, false);

    int best_score = -1000000;
    uint32_t best_baud = old_baud;
    const size_t ncand = sizeof(s_can_baud_cands) / sizeof(s_can_baud_cands[0]);

    for (size_t i = 0; i < ncand; i++) {
        uint32_t baud = s_can_baud_cands[i];
        esp_err_t e = bus_app_set_can_baud(baud);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "can cand %lu start fail: %s",
                     (unsigned long)baud, esp_err_to_name(e));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(30));

        bus_can_status_t c0;
        bus_ch_stats_t ch0[BUS_SRC_COUNT];
        bus_can_get_status(&c0);
        bus_capture_get_ch_stats(ch0);

        vTaskDelay(pdMS_TO_TICKS(sample_ms));

        bus_can_status_t c1;
        bus_ch_stats_t ch1[BUS_SRC_COUNT];
        bus_can_get_status(&c1);
        bus_capture_get_ch_stats(ch1);

        int32_t rx_d = (int32_t)(ch1[BUS_SRC_CAN].rx_count - ch0[BUS_SRC_CAN].rx_count);
        int32_t err_d = (int32_t)(ch1[BUS_SRC_CAN].err_count - ch0[BUS_SRC_CAN].err_count);
        int32_t good = rx_d - err_d;
        if (good < 0) {
            good = 0;
        }
        int32_t bit_d = (int32_t)(c1.err_bit - c0.err_bit);
        int32_t form_d = (int32_t)(c1.err_form - c0.err_form);
        int32_t stuff_d = (int32_t)(c1.err_stuff - c0.err_stuff);
        int32_t hard_err = bit_d + form_d + stuff_d;
        if (hard_err < 0) {
            hard_err = 0;
        }

        /* 有效帧加权，协议错严重扣分；仅有噪声无帧 → 负分 */
        int score = (int)(good * 40 + rx_d * 5 - err_d * 15 - hard_err * 8);
        if (good >= 2 && hard_err == 0) {
            score += 80;
        }
        ESP_LOGI(TAG, "can baud %lu score=%d good=%ld rx=%ld err=%ld hard=%ld",
                 (unsigned long)baud, score, (long)good, (long)rx_d,
                 (long)err_d, (long)hard_err);
        if (score > best_score) {
            best_score = score;
            best_baud = baud;
        }
    }

    esp_err_t err;
    if (best_score < 20) {
        ESP_LOGW(TAG, "can auto: no clear baud (best=%d); keep %lu",
                 best_score, (unsigned long)old_baud);
        best_baud = old_baud;
        err = ESP_ERR_NOT_FOUND;
    } else {
        err = ESP_OK;
    }

    (void)bus_app_set_can_baud(best_baud);
    (void)bus_app_set_can_filter(filt_en, filt_id, filt_mask, filt_ext);
    (void)bus_app_set_listen(old_listen);

    if (out_baud) {
        *out_baud = best_baud;
    }
    ESP_LOGI(TAG, "can autodetect -> %lu (score=%d)", (unsigned long)best_baud, best_score);
    return err;
}

static const uint32_t s_rs485_baud_cands[] = {
    9600, 19200, 38400, 57600, 115200, 230400
};

static uart_parity_t map_rs_parity(bus_rs485_parity_t p)
{
    switch (p) {
    case BUS_RS485_PARITY_EVEN: return UART_PARITY_EVEN;
    case BUS_RS485_PARITY_ODD:  return UART_PARITY_ODD;
    default:                    return UART_PARITY_DISABLE;
    }
}

static uint16_t rs_modbus_crc(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static int score_rs485_bytes(const uint8_t *buf, int n)
{
    int sc = score_rx_bytes(buf, n);
    if (n >= 4 && n <= 64) {
        uint16_t got = (uint16_t)buf[n - 2] | ((uint16_t)buf[n - 1] << 8);
        if (rs_modbus_crc(buf, (size_t)n - 2) == got && buf[0] >= 1 && buf[0] <= 247) {
            sc += 120;
        }
    }
    /* 滑动找短帧 CRC */
    for (int i = 0; i + 8 <= n; i++) {
        for (int L = 4; L <= 8 && i + L <= n; L++) {
            uint16_t got = (uint16_t)buf[i + L - 2] | ((uint16_t)buf[i + L - 1] << 8);
            if (rs_modbus_crc(buf + i, (size_t)L - 2) == got) {
                sc += 40;
            }
        }
    }
    return sc;
}

esp_err_t bus_tools_rs485_autodetect(uint32_t *out_baud, bus_rs485_parity_t *out_parity,
                                     int sample_ms)
{
    if (sample_ms < 60) {
        sample_ms = 60;
    }
    if (sample_ms > 400) {
        sample_ms = 400;
    }

    uint32_t old_baud = bus_rs485_get_baud();
    bus_rs485_parity_t old_par = bus_rs485_get_parity();
    bool was_run = bus_rs485_is_running();

    if (was_run) {
        (void)bus_rs485_stop();
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    /* DE=0 保持接收 */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BUS_RS485_DE_RE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io);
    gpio_set_level(BUS_RS485_DE_RE_GPIO, 0);

    esp_err_t err = uart_driver_install(BUS_RS485_UART_NUM, 2048, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "rs485 uart install: %s", esp_err_to_name(err));
        if (was_run) {
            (void)bus_app_set_rs485_baud(old_baud);
            (void)bus_app_set_rs485_parity(old_par);
        }
        return err;
    }
    (void)uart_set_pin(BUS_RS485_UART_NUM, BUS_RS485_TX_GPIO, BUS_RS485_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    static const bus_rs485_parity_t pars[] = {
        BUS_RS485_PARITY_NONE, BUS_RS485_PARITY_EVEN, BUS_RS485_PARITY_ODD
    };
    int best_score = -1;
    uint32_t best_baud = old_baud ? old_baud : BUS_RS485_BAUD_DEFAULT;
    bus_rs485_parity_t best_par = old_par;
    uint8_t buf[256];

    uart_config_t cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    const size_t nb = sizeof(s_rs485_baud_cands) / sizeof(s_rs485_baud_cands[0]);
    for (size_t i = 0; i < nb; i++) {
        for (size_t p = 0; p < sizeof(pars) / sizeof(pars[0]); p++) {
            cfg.baud_rate = (int)s_rs485_baud_cands[i];
            cfg.parity = map_rs_parity(pars[p]);
            if (uart_param_config(BUS_RS485_UART_NUM, &cfg) != ESP_OK) {
                continue;
            }
            (void)uart_flush_input(BUS_RS485_UART_NUM);
            vTaskDelay(pdMS_TO_TICKS(sample_ms));
            int n = uart_read_bytes(BUS_RS485_UART_NUM, buf, sizeof(buf), 0);
            int sc = score_rs485_bytes(buf, n);
            ESP_LOGI(TAG, "rs485 %lu p=%d score=%d n=%d",
                     (unsigned long)s_rs485_baud_cands[i], (int)pars[p], sc, n);
            if (sc > best_score) {
                best_score = sc;
                best_baud = s_rs485_baud_cands[i];
                best_par = pars[p];
            }
        }
    }

    uart_driver_delete(BUS_RS485_UART_NUM);

    if (best_score <= 0) {
        ESP_LOGW(TAG, "rs485 auto: no clear; keep %lu p=%d",
                 (unsigned long)old_baud, (int)old_par);
        best_baud = old_baud ? old_baud : BUS_RS485_BAUD_DEFAULT;
        best_par = old_par;
        err = ESP_ERR_NOT_FOUND;
    } else {
        err = ESP_OK;
    }

    (void)bus_app_set_rs485_parity(best_par);
    (void)bus_app_set_rs485_baud(best_baud);

    if (out_baud) {
        *out_baud = best_baud;
    }
    if (out_parity) {
        *out_parity = best_par;
    }
    ESP_LOGI(TAG, "rs485 autodetect -> %lu p=%d (score=%d)",
             (unsigned long)best_baud, (int)best_par, best_score);
    return err;
}

int bus_tools_i2c_identify(char *out, size_t out_sz)
{
    if (!out || out_sz < 8) {
        return -1;
    }
    uint8_t found[32];
    int n = bus_i2c_scan(found, (int)(sizeof(found)));
    if (n < 0) {
        snprintf(out, out_sz, "I2C scan fail");
        return n;
    }
    size_t p = 0;
    p += (size_t)snprintf(out + p, out_sz - p, "I2C %d dev:", n);
    for (int i = 0; i < n && p + 16 < out_sz; i++) {
        const char *nm = bus_tools_i2c_name(found[i]);
        if (nm) {
            p += (size_t)snprintf(out + p, out_sz - p, " %02X(%s)", found[i], nm);
        } else {
            p += (size_t)snprintf(out + p, out_sz - p, " %02X", found[i]);
        }
    }
    if (n == 0) {
        snprintf(out, out_sz, "I2C 0 devices");
    }
    return n;
}

esp_err_t bus_tools_i2c_dump(uint8_t addr7, uint16_t reg, uint8_t addr_bytes,
                             uint8_t *buf, size_t len)
{
    if (!buf || len == 0 || len > 256) {
        return ESP_ERR_INVALID_ARG;
    }
    if (addr_bytes != 1 && addr_bytes != 2) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t wr[2];
    size_t wr_len;
    if (addr_bytes == 1) {
        wr[0] = (uint8_t)(reg & 0xFFu);
        wr_len = 1;
    } else {
        wr[0] = (uint8_t)((reg >> 8) & 0xFFu);
        wr[1] = (uint8_t)(reg & 0xFFu);
        wr_len = 2;
    }
    return bus_i2c_write_read(addr7, wr, wr_len, buf, len);
}

static const char *jedec_mfr_name(uint8_t mfr)
{
    switch (mfr) {
    case 0x01: return "Cypress/Spansion";
    case 0x1F: return "Adesto/Atmel";
    case 0x20: return "Micron/Numonyx";
    case 0x37: return "AMIC";
    case 0x9D: return "ISSI";
    case 0xC2: return "MXIC";
    case 0xC8: return "GigaDevice";
    case 0xEF: return "Winbond";
    default: return "Unknown";
    }
}

esp_err_t bus_tools_spi_jedec(uint8_t id[3], char *name_out, size_t name_sz)
{
    if (!id) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t tx[4] = {0x9F, 0x00, 0x00, 0x00};
    uint8_t rx[4] = {0};
    esp_err_t err = bus_spi_xfer(tx, rx, 4);
    if (err != ESP_OK) {
        return err;
    }
    id[0] = rx[1];
    id[1] = rx[2];
    id[2] = rx[3];
    if (name_out && name_sz) {
        unsigned cap_mb = 0;
        if (id[2] >= 0x10 && id[2] <= 0x20) {
            cap_mb = 1u << (id[2] - 0x10); /* rough power-of-two MiB from density byte */
        }
        snprintf(name_out, name_sz, "%s %02X%02X%02X ~%uMb",
                 jedec_mfr_name(id[0]), id[0], id[1], id[2], cap_mb ? cap_mb * 8 : 0);
    }
    return ESP_OK;
}

esp_err_t bus_tools_spi_flash_read(uint32_t addr, uint8_t *buf, size_t len)
{
    if (!buf || len == 0 || len > 256) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t tx[4 + 256];
    uint8_t rx[4 + 256];
    tx[0] = 0x03;
    tx[1] = (uint8_t)((addr >> 16) & 0xFFu);
    tx[2] = (uint8_t)((addr >> 8) & 0xFFu);
    tx[3] = (uint8_t)(addr & 0xFFu);
    memset(tx + 4, 0xFF, len);
    esp_err_t err = bus_spi_xfer(tx, rx, 4 + len);
    if (err != ESP_OK) {
        return err;
    }
    memcpy(buf, rx + 4, len);
    return ESP_OK;
}
