/**
 * @file bus_can.h
 * @brief TWAI/CAN（esp_twai 新驱动，TJA1050）
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "bus_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bus_can_frame_cb_t)(const bus_frame_t *frame, void *user);

typedef struct {
    bool running;
    bool listen_only;
    bool filter_en;
    bool filter_ext;
    uint32_t baud;
    uint32_t filter_id;
    uint32_t filter_mask;
    uint16_t tec;
    uint16_t rec;
    uint32_t bus_err;
    uint32_t state; /* twai_error_state_t */
    uint32_t err_arb;
    uint32_t err_bit;
    uint32_t err_form;
    uint32_t err_stuff;
    uint32_t err_ack;
    char last_err[40];
} bus_can_status_t;

esp_err_t bus_can_start(uint32_t baud, bool listen_only);
esp_err_t bus_can_stop(void);

bool bus_can_is_running(void);
uint32_t bus_can_get_baud(void);
bool bus_can_get_listen_only(void);

void bus_can_set_allow_tx(bool allow);
bool bus_can_get_allow_tx(void);

/**
 * @brief 硬件掩码滤波（需在运行中：内部 disable→配→enable）
 * @param enable false=接收全部
 * @param id     匹配基 ID
 * @param mask   1=该位必须匹配，0=任意（标准帧常用 mask=0x7FF 精确匹配）
 * @param ext    扩展帧滤波
 */
esp_err_t bus_can_set_filter(bool enable, uint32_t id, uint32_t mask, bool ext);

esp_err_t bus_can_send(uint32_t id, bool ext, const uint8_t *data, size_t len);

void bus_can_get_status(bus_can_status_t *out);

void bus_can_set_rx_cb(bus_can_frame_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif
