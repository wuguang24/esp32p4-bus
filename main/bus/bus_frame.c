/**
 * @file bus_frame.c
 */

#include "bus_frame.h"
#include "esp_timer.h"

void bus_frame_stamp(bus_frame_t *f)
{
    if (!f) {
        return;
    }
    uint64_t us = (uint64_t)esp_timer_get_time();
    f->t_us = us;
    f->t_ms = (uint32_t)(us / 1000ULL);
}
