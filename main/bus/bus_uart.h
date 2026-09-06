#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "bus_frame.h"
#include "bus_framer.h"
#include "bus_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bus_uart_frame_cb_t)(const bus_frame_t *frame, void *user);

esp_err_t bus_uart_start(uint32_t baud, bus_uart_parity_t parity, bus_uart_stop_t stop);
esp_err_t bus_uart_stop(void);
bool bus_uart_is_running(void);
uint32_t bus_uart_get_baud(void);
bus_uart_parity_t bus_uart_get_parity(void);
bus_uart_stop_t bus_uart_get_stop(void);
void bus_uart_set_allow_tx(bool allow);
esp_err_t bus_uart_send(const uint8_t *buf, size_t len);
void bus_uart_set_rx_cb(bus_uart_frame_cb_t cb, void *user);
void bus_uart_set_framer(const bus_framer_cfg_t *cfg);
void bus_uart_get_framer(bus_framer_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
