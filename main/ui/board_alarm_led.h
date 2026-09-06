/**
 * @file board_alarm_led.h
 * @brief 板载双色 LED 报警灯效（LED0=红 GPIO14，LED1=绿 GPIO13，低电平点亮）
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void board_alarm_led_init(void);
/** 在 ui_shell 定时器中调用（约 50ms） */
void board_alarm_led_poll(void);

#ifdef __cplusplus
}
#endif
