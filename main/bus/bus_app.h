/**
 * @file bus_app.h
 * @brief 五总线分析仪总控
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bus_capture.h"
#include "bus_framer.h"
#include "bus_logger.h"
#include "bus_pins.h"
#include "bus_can.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool started;
    bool can_running;
    bool rs485_running;
    bool uart_running;
    bool i2c_running;
    bool spi_running;
    bool listen_only;
    bool allow_tx;
    bool sd_mounted;
    bool recording;
    uint32_t can_baud;
    uint32_t rs485_baud;
    uint32_t uart_baud;
    uint32_t i2c_hz;
    uint32_t spi_hz;
    uint8_t spi_mode;
    bus_rs485_parity_t rs485_parity;
    bus_uart_parity_t uart_parity;
    bus_uart_stop_t uart_stop;
    bus_can_status_t can;
    bus_capture_stats_t stats;
    bus_ch_stats_t ch[BUS_SRC_COUNT];
    bus_logger_status_t logger;
} bus_app_status_t;

esp_err_t bus_app_start(void);
esp_err_t bus_app_stop(void);

esp_err_t bus_app_set_can_baud(uint32_t baud);
esp_err_t bus_app_set_rs485_baud(uint32_t baud);
esp_err_t bus_app_set_rs485_parity(bus_rs485_parity_t parity);
esp_err_t bus_app_set_uart_baud(uint32_t baud);
esp_err_t bus_app_set_uart_parity(bus_uart_parity_t parity);
esp_err_t bus_app_set_uart_stop(bus_uart_stop_t stop);
esp_err_t bus_app_set_i2c_hz(uint32_t hz);
esp_err_t bus_app_set_spi_hz(uint32_t hz);
esp_err_t bus_app_set_spi_mode(uint8_t mode);
esp_err_t bus_app_set_listen(bool listen_only);
esp_err_t bus_app_set_allow_tx(bool allow);
esp_err_t bus_app_set_can_filter(bool enable, uint32_t id, uint32_t mask, bool ext);

/** 将当前总线参数写入 NVS（改波特率等后会自动调用） */
esp_err_t bus_app_settings_save(void);
/** 恢复出厂默认并重启各总线 */
esp_err_t bus_app_settings_reset(void);

esp_err_t bus_app_logger_start(void);
esp_err_t bus_app_logger_stop(void);

/** 重新挂载 TF（插拔后可点设置页「SD」） */
esp_err_t bus_app_sd_remount(void);

void bus_app_get_stats(bus_capture_stats_t *out);
void bus_app_get_status(bus_app_status_t *out);
void bus_app_clear_capture(void);

/** Compose TX（需 allow_tx；CAN 另需非 listen_only） */
esp_err_t bus_app_can_send(uint32_t id, bool ext, const uint8_t *data, size_t len);
esp_err_t bus_app_uart_send(const uint8_t *data, size_t len);
esp_err_t bus_app_rs485_send(const uint8_t *data, size_t len);

/** UART/RS485 抓取帧规：空闲超时 / 定长 / 帧头+帧尾（键盘可改长、头、尾字节） */
void bus_app_set_uart_framer(const bus_framer_cfg_t *cfg);
void bus_app_get_uart_framer(bus_framer_cfg_t *cfg);
void bus_app_set_rs485_framer(const bus_framer_cfg_t *cfg);
void bus_app_get_rs485_framer(bus_framer_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
