/**
 * @file bus_app.c
 * @brief 五总线总控：CAN/RS485/UART/I2C/SPI + SD
 */

#include "bus_app.h"

#include "esp_log.h"
#include "sdmmc.h"

#include "bus_can.h"
#include "bus_rs485.h"
#include "bus_uart.h"
#include "bus_i2c.h"
#include "bus_spi.h"

static const char *TAG = "bus_app";

static bool s_started;
static bool s_sd_ok;
static bool s_listen_only = true;
static bool s_allow_tx;
static uint32_t s_can_baud = BUS_CAN_BAUD_DEFAULT;
static uint32_t s_rs485_baud = BUS_RS485_BAUD_DEFAULT;
static uint32_t s_uart_baud = BUS_UART_BAUD_DEFAULT;
static uint32_t s_i2c_hz = BUS_I2C_FREQ_DEFAULT_HZ;
static uint32_t s_spi_hz = BUS_SPI_FREQ_DEFAULT_HZ;
static uint8_t s_spi_mode;
static bus_rs485_parity_t s_rs485_parity = BUS_RS485_PARITY_NONE;
static bus_uart_parity_t s_uart_parity = BUS_UART_PARITY_NONE;
static bus_uart_stop_t s_uart_stop = BUS_UART_STOP_1;

static void sync_link_rates(void)
{
    bus_capture_set_link_rate(BUS_SRC_CAN, s_can_baud);
    bus_capture_set_link_rate(BUS_SRC_RS485, s_rs485_baud);
    bus_capture_set_link_rate(BUS_SRC_UART, s_uart_baud);
    bus_capture_set_link_rate(BUS_SRC_I2C, s_i2c_hz);
    bus_capture_set_link_rate(BUS_SRC_SPI, s_spi_hz);
}

static esp_err_t restart_uart(void)
{
    if (!s_started) {
        return ESP_OK;
    }
    esp_err_t err = bus_uart_stop();
    if (err != ESP_OK) {
        return err;
    }
    err = bus_uart_start(s_uart_baud, s_uart_parity, s_uart_stop);
    if (err == ESP_OK) {
        bus_uart_set_allow_tx(s_allow_tx);
    }
    return err;
}

static esp_err_t restart_rs485(void)
{
    if (!s_started) {
        return ESP_OK;
    }
    esp_err_t err = bus_rs485_stop();
    if (err != ESP_OK) {
        return err;
    }
    err = bus_rs485_start(s_rs485_baud, s_rs485_parity);
    if (err == ESP_OK) {
        bus_rs485_set_allow_tx(s_allow_tx);
    }
    return err;
}

esp_err_t bus_app_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    esp_err_t err = bus_capture_init();
    if (err != ESP_OK) {
        return err;
    }
    sync_link_rates();

    err = sdmmc_init();
    s_sd_ok = (err == ESP_OK && sdmmc_mount_flag == 0x01);
    if (!s_sd_ok) {
        ESP_LOGW(TAG, "SD mount skipped: %s", esp_err_to_name(err));
    }

    err = bus_can_start(s_can_baud, s_listen_only);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CAN start fail: %s", esp_err_to_name(err));
        return err;
    }
    bus_can_set_allow_tx(s_allow_tx);

    err = bus_rs485_start(s_rs485_baud, s_rs485_parity);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RS485 start fail: %s", esp_err_to_name(err));
        bus_can_stop();
        return err;
    }
    bus_rs485_set_allow_tx(s_allow_tx);

    err = bus_uart_start(s_uart_baud, s_uart_parity, s_uart_stop);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "UART start fail: %s (continue)", esp_err_to_name(err));
    } else {
        bus_uart_set_allow_tx(s_allow_tx);
    }

    err = bus_i2c_start(s_i2c_hz);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C start fail: %s (continue)", esp_err_to_name(err));
    }

    err = bus_spi_start(s_spi_hz, s_spi_mode);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPI start fail: %s (continue)", esp_err_to_name(err));
    }

    s_started = true;
    ESP_LOGI(TAG, "bus analyzer up CAN=%lu RS485=%lu UART=%lu I2C=%lu SPI=%lu/%u sd=%d",
             (unsigned long)s_can_baud, (unsigned long)s_rs485_baud,
             (unsigned long)s_uart_baud, (unsigned long)s_i2c_hz,
             (unsigned long)s_spi_hz, (unsigned)s_spi_mode, (int)s_sd_ok);
    return ESP_OK;
}

esp_err_t bus_app_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }
    (void)bus_logger_stop();
    (void)bus_spi_stop();
    (void)bus_i2c_stop();
    (void)bus_uart_stop();
    (void)bus_rs485_stop();
    (void)bus_can_stop();
    s_started = false;
    return ESP_OK;
}

esp_err_t bus_app_set_can_baud(uint32_t baud)
{
    if (baud < BUS_CAN_BAUD_MIN || baud > BUS_CAN_BAUD_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    s_can_baud = baud;
    bus_capture_set_link_rate(BUS_SRC_CAN, baud);
    if (!s_started) {
        return ESP_OK;
    }
    bool lo = s_listen_only;
    bool allow = s_allow_tx;
    esp_err_t err = bus_can_stop();
    if (err != ESP_OK) {
        return err;
    }
    err = bus_can_start(s_can_baud, lo);
    if (err == ESP_OK) {
        bus_can_set_allow_tx(allow);
    }
    return err;
}

esp_err_t bus_app_set_rs485_baud(uint32_t baud)
{
    if (baud < BUS_RS485_BAUD_MIN || baud > BUS_RS485_BAUD_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    s_rs485_baud = baud;
    bus_capture_set_link_rate(BUS_SRC_RS485, baud);
    return restart_rs485();
}

esp_err_t bus_app_set_rs485_parity(bus_rs485_parity_t parity)
{
    s_rs485_parity = parity;
    return restart_rs485();
}

esp_err_t bus_app_set_uart_baud(uint32_t baud)
{
    if (baud == 0 || baud > BUS_UART_BAUD_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    s_uart_baud = baud;
    bus_capture_set_link_rate(BUS_SRC_UART, baud);
    return restart_uart();
}

esp_err_t bus_app_set_uart_parity(bus_uart_parity_t parity)
{
    s_uart_parity = parity;
    return restart_uart();
}

esp_err_t bus_app_set_uart_stop(bus_uart_stop_t stop)
{
    s_uart_stop = stop;
    return restart_uart();
}

esp_err_t bus_app_set_i2c_hz(uint32_t hz)
{
    if (hz < 10000u || hz > 1000000u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_i2c_hz == hz && s_started && bus_i2c_is_running()) {
        bus_capture_set_link_rate(BUS_SRC_I2C, hz);
        return ESP_OK;
    }
    s_i2c_hz = hz;
    bus_capture_set_link_rate(BUS_SRC_I2C, hz);
    if (!s_started) {
        return ESP_OK;
    }
    (void)bus_i2c_stop();
    return bus_i2c_start(s_i2c_hz);
}

esp_err_t bus_app_set_spi_hz(uint32_t hz)
{
    if (hz < 10000u || hz > 40000000u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_spi_hz == hz && s_started && bus_spi_is_running()) {
        bus_capture_set_link_rate(BUS_SRC_SPI, hz);
        return ESP_OK;
    }
    s_spi_hz = hz;
    bus_capture_set_link_rate(BUS_SRC_SPI, hz);
    if (!s_started) {
        return ESP_OK;
    }
    (void)bus_spi_stop();
    return bus_spi_start(s_spi_hz, s_spi_mode);
}

esp_err_t bus_app_set_spi_mode(uint8_t mode)
{
    if (mode > 3) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_spi_mode == mode && s_started && bus_spi_is_running()) {
        return ESP_OK;
    }
    s_spi_mode = mode;
    if (!s_started) {
        return ESP_OK;
    }
    (void)bus_spi_stop();
    return bus_spi_start(s_spi_hz, s_spi_mode);
}

esp_err_t bus_app_set_listen(bool listen_only)
{
    s_listen_only = listen_only;
    if (!s_started) {
        return ESP_OK;
    }
    bool allow = s_allow_tx;
    esp_err_t err = bus_can_stop();
    if (err != ESP_OK) {
        return err;
    }
    err = bus_can_start(s_can_baud, s_listen_only);
    if (err == ESP_OK) {
        bus_can_set_allow_tx(allow);
    }
    return err;
}

esp_err_t bus_app_set_allow_tx(bool allow)
{
    s_allow_tx = allow;
    bus_can_set_allow_tx(allow);
    bus_uart_set_allow_tx(allow);
    bus_rs485_set_allow_tx(allow);
    return ESP_OK;
}

esp_err_t bus_app_set_can_filter(bool enable, uint32_t id, uint32_t mask, bool ext)
{
    return bus_can_set_filter(enable, id, mask, ext);
}

esp_err_t bus_app_logger_start(void)
{
    return bus_logger_start();
}

esp_err_t bus_app_logger_stop(void)
{
    return bus_logger_stop();
}

esp_err_t bus_app_sd_remount(void)
{
    if (bus_logger_is_recording()) {
        (void)bus_logger_stop();
    }
    (void)sdmmc_unmount();
    esp_err_t err = sdmmc_init();
    s_sd_ok = (err == ESP_OK && sdmmc_mount_flag == 0x01);
    ESP_LOGI(TAG, "SD remount %s", s_sd_ok ? "OK" : "FAIL");
    return s_sd_ok ? ESP_OK : ESP_FAIL;
}

void bus_app_get_stats(bus_capture_stats_t *out)
{
    bus_capture_get_stats(out);
}

void bus_app_clear_capture(void)
{
    bus_capture_clear();
}

esp_err_t bus_app_can_send(uint32_t id, bool ext, const uint8_t *data, size_t len)
{
    if (!s_started || !s_allow_tx) {
        return ESP_ERR_INVALID_STATE;
    }
    return bus_can_send(id, ext, data, len);
}

esp_err_t bus_app_uart_send(const uint8_t *data, size_t len)
{
    if (!s_started || !s_allow_tx) {
        return ESP_ERR_INVALID_STATE;
    }
    return bus_uart_send(data, len);
}

esp_err_t bus_app_rs485_send(const uint8_t *data, size_t len)
{
    if (!s_started || !s_allow_tx) {
        return ESP_ERR_INVALID_STATE;
    }
    return bus_rs485_send(data, len);
}

void bus_app_set_uart_framer(const bus_framer_cfg_t *cfg)
{
    bus_uart_set_framer(cfg);
}

void bus_app_get_uart_framer(bus_framer_cfg_t *cfg)
{
    bus_uart_get_framer(cfg);
}

void bus_app_set_rs485_framer(const bus_framer_cfg_t *cfg)
{
    bus_rs485_set_framer(cfg);
}

void bus_app_get_rs485_framer(bus_framer_cfg_t *cfg)
{
    bus_rs485_get_framer(cfg);
}

void bus_app_get_status(bus_app_status_t *out)
{
    if (out == NULL) {
        return;
    }
    out->started = s_started;
    out->can_running = bus_can_is_running();
    out->rs485_running = bus_rs485_is_running();
    out->uart_running = bus_uart_is_running();
    out->i2c_running = bus_i2c_is_running();
    out->spi_running = bus_spi_is_running();
    out->listen_only = s_listen_only;
    out->allow_tx = s_allow_tx;
    out->sd_mounted = s_sd_ok || (sdmmc_mount_flag == 0x01);
    out->recording = bus_logger_is_recording();
    out->can_baud = bus_can_get_baud();
    out->rs485_baud = bus_rs485_get_baud();
    out->uart_baud = bus_uart_get_baud();
    out->i2c_hz = bus_i2c_is_running() ? bus_i2c_get_freq() : s_i2c_hz;
    out->spi_hz = bus_spi_is_running() ? bus_spi_get_freq() : s_spi_hz;
    out->spi_mode = bus_spi_is_running() ? bus_spi_get_mode() : s_spi_mode;
    out->rs485_parity = bus_rs485_is_running() ? bus_rs485_get_parity() : s_rs485_parity;
    out->uart_parity = bus_uart_is_running() ? bus_uart_get_parity() : s_uart_parity;
    out->uart_stop = bus_uart_is_running() ? bus_uart_get_stop() : s_uart_stop;
    bus_can_get_status(&out->can);
    bus_capture_get_stats(&out->stats);
    bus_capture_get_ch_stats(out->ch);
    bus_logger_get_status(&out->logger);
}
