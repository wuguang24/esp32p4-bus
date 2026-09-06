/**
 * @file ui_scope.h
 * @brief 示波器风格负载/活动波形（协议层，非 GPIO LA）
 */

#pragma once

#include "lvgl.h"
#include "bus_capture.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_SCOPE_MAX_SER 5

typedef struct {
    lv_obj_t *chart;
    lv_chart_series_t *ser[UI_SCOPE_MAX_SER];
    uint8_t src[UI_SCOPE_MAX_SER];
    uint8_t n_ser;
    uint32_t last_seq;
} ui_scope_t;

/** 创建示波腔；srcs 长度 n，颜色自动按通道 */
void ui_scope_init(ui_scope_t *sc, lv_obj_t *parent, int32_t h,
                   const uint8_t *srcs, uint8_t n);

/** 刷新负载历史 0~100%（hist 未变则跳过） */
void ui_scope_update_load(ui_scope_t *sc);

#ifdef __cplusplus
}
#endif
