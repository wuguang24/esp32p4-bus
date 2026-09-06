/**
 * @file bus_decode.h
 * @brief 轻量协议注解（CAN / UART ASCII / Modbus RTU）
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "bus_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

void bus_decode_annotate(bus_frame_t *f);

/** Modbus 功能码中文名；未知返回 "FC?x" */
const char *bus_decode_modbus_fc_name(uint8_t fc);

/**
 * @param prev_us 上一行时间戳；0=绝对时间，否则 +Δus
 */
void bus_decode_format_line(const bus_frame_t *f, uint64_t prev_us, char *out, size_t out_sz);

void bus_decode_format_detail(const bus_frame_t *f, char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif
