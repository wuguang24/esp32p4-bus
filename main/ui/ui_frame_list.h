/**
 * @file ui_frame_list.h
 * @brief 示波腔帧列表：µs 解码行 + 点选详情
 */

#pragma once

#include "lvgl.h"
#include "bus_frame.h"
#include "bus_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_frame_list_select_cb_t)(const bus_frame_t *f, void *user);

typedef struct {
    lv_obj_t *cont;
    lv_obj_t *rows[BUS_LIST_ROWS];
    bus_frame_t cache[BUS_LIST_ROWS];
    uint32_t row_col[BUS_LIST_ROWS];
    uint8_t row_sel_opa[BUS_LIST_ROWS];
    size_t cache_n;
    int row_n;
    int selected;
    bool follow_tail;
    ui_frame_list_select_cb_t on_select;
    void *on_select_user;
} ui_frame_list_t;

void ui_frame_list_init(ui_frame_list_t *fl, lv_obj_t *parent);
void ui_frame_list_set_select_cb(ui_frame_list_t *fl, ui_frame_list_select_cb_t cb, void *user);
/** 用 frames[0..n) 刷新；follow_tail 时滚到底 */
void ui_frame_list_update(ui_frame_list_t *fl, const bus_frame_t *frames, size_t n, bool follow_tail);
void ui_frame_list_clear(ui_frame_list_t *fl);

#ifdef __cplusplus
}
#endif
