/**
 * @file ui_pages.h
 * @brief 总线分析仪 LVGL 页面
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_page_overview_create(lv_obj_t *parent);
void ui_page_overview_update(void);

lv_obj_t *ui_page_bus_create(lv_obj_t *parent);
void ui_page_bus_update(void);

lv_obj_t *ui_page_uart_create(lv_obj_t *parent);
void ui_page_uart_update(void);

lv_obj_t *ui_page_io_create(lv_obj_t *parent);
void ui_page_io_update(void);

lv_obj_t *ui_page_setup_create(lv_obj_t *parent);
void ui_page_setup_update(void);

void ui_shell_create(void);
/** 在 lv_demo_task 主循环调用；返回 true 表示还有预加载工作 */
bool ui_shell_pump(void);

#ifdef __cplusplus
}
#endif
