/**
 * @file ui_numpad.h
 * @brief 全屏数字键盘弹窗（触摸输入参数）
 */

#pragma once

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_numpad_cb_t)(uint32_t value, void *user_data);

/**
 * @brief 弹出数字键盘
 * @param title     标题（英文/数字，走 montserrat）
 * @param hint      范围提示，如 "100~40000"
 * @param initial   初始值
 * @param min_v     最小值（含）
 * @param max_v     最大值（含）
 * @param cb        确认回调；取消不调用
 * @param user_data 回调透传
 */
void ui_numpad_open(const char *title, const char *hint,
                    uint32_t initial, uint32_t min_v, uint32_t max_v,
                    ui_numpad_cb_t cb, void *user_data);

void ui_numpad_close(void);
bool ui_numpad_is_open(void);

#ifdef __cplusplus
}
#endif
