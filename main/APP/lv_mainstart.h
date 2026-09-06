/**
 ******************************************************************************
 * @file        lv_mainstart.h
 * @brief       Boot splash then hand-off to HMI shell
 ******************************************************************************
 */

#ifndef __LV_MAINSTART_H
#define __LV_MAINSTART_H

/** 显示品牌闪屏并启动揭示动画 */
void lv_mainstart(void);

/** 进度收尾（满条 +「就绪」），在淡入主界面前调用 */
void lv_splash_finish(void);

/** 丢弃闪屏对象引用（旧屏即将被 load_anim 删除时必须调用） */
void lv_splash_clear(void);

#endif
