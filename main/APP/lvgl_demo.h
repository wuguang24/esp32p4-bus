/**
 ******************************************************************************
 * @file        lvgl_demo.h
 * @brief       LVGL V9 port (display + touch)
 ******************************************************************************
 */

#ifndef __LVGL_DEMO_H
#define __LVGL_DEMO_H

#include "lvgl.h"

void lvgl_demo(void);
lv_display_t *lv_port_disp_init(void);
lv_indev_t *lv_port_indev_init(void);
void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);

#endif
