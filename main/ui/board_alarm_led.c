/**
 * @file board_alarm_led.c
 * @brief 总线状态 → 双色 LED
 */

#include "board_alarm_led.h"
#include "led.h"
#include "bus_app.h"
#include "esp_timer.h"

#define LED_RED_ON()    LED0(0)
#define LED_RED_OFF()   LED0(1)
#define LED_GREEN_ON()  LED1(0)
#define LED_GREEN_OFF() LED1(1)

void board_alarm_led_init(void)
{
    LED_RED_OFF();
    LED_GREEN_OFF();
}

void board_alarm_led_poll(void)
{
    bus_app_status_t st;
    bus_app_get_status(&st);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

    if (st.stats.err_count > 0 && ((now / 200) & 1)) {
        LED_RED_ON();
        LED_GREEN_OFF();
        return;
    }
    if (st.recording) {
        LED_RED_OFF();
        LED_GREEN_ON();
        return;
    }
    if (st.stats.rx_rate > 0.5f) {
        LED_RED_OFF();
        if ((now / 400) & 1) {
            LED_GREEN_ON();
        } else {
            LED_GREEN_OFF();
        }
        return;
    }
    LED_RED_OFF();
    LED_GREEN_OFF();
}
