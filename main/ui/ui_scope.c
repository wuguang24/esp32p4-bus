/**
 * @file ui_scope.c
 */

#include "ui_scope.h"
#include "ui_theme.h"

static uint32_t src_color(uint8_t src)
{
    switch (src) {
    case BUS_SRC_CAN:   return UI_COL_SCOPE_CH1;
    case BUS_SRC_RS485: return UI_COL_SCOPE_CH2;
    case BUS_SRC_UART:  return UI_COL_SCOPE_CH3;
    case BUS_SRC_I2C:   return UI_COL_TX;
    case BUS_SRC_SPI:   return UI_COL_STEEL;
    default:            return UI_COL_ACCENT;
    }
}

void ui_scope_init(ui_scope_t *sc, lv_obj_t *parent, int32_t h,
                   const uint8_t *srcs, uint8_t n)
{
    if (!sc || !parent || !srcs || n == 0) {
        return;
    }
    if (n > UI_SCOPE_MAX_SER) {
        n = UI_SCOPE_MAX_SER;
    }
    sc->n_ser = n;
    sc->last_seq = 0;
    for (uint8_t i = 0; i < n; i++) {
        sc->src[i] = srcs[i];
    }

    sc->chart = lv_chart_create(parent);
    lv_obj_set_width(sc->chart, lv_pct(100));
    lv_obj_set_height(sc->chart, h);
    ui_style_scope_face(sc->chart);
    lv_obj_set_style_pad_all(sc->chart, 2, 0);
    lv_chart_set_type(sc->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(sc->chart, BUS_RATE_HIST_LEN);
    lv_chart_set_range(sc->chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(sc->chart, 3, 6);
    lv_chart_set_update_mode(sc->chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_line_color(sc->chart, ui_color(UI_COL_SCOPE_GRID), LV_PART_MAIN);
    lv_obj_set_style_line_opa(sc->chart, LV_OPA_30, LV_PART_MAIN);
    ui_style_scope_trace(sc->chart);

    for (uint8_t i = 0; i < n; i++) {
        sc->ser[i] = lv_chart_add_series(sc->chart, ui_color(src_color(srcs[i])),
                                         LV_CHART_AXIS_PRIMARY_Y);
    }
}

void ui_scope_update_load(ui_scope_t *sc)
{
    if (!sc || !sc->chart) {
        return;
    }
    uint32_t seq = bus_capture_hist_seq();
    if (seq == sc->last_seq && seq != 0) {
        return;
    }

    uint8_t hist[BUS_RATE_HIST_LEN];
    const bool full = (sc->last_seq == 0) || (seq - sc->last_seq > 3u);
    sc->last_seq = seq;

    for (uint8_t s = 0; s < sc->n_ser; s++) {
        if (!sc->ser[s]) {
            continue;
        }
        size_t n = bus_capture_get_load_hist(sc->src[s], hist, BUS_RATE_HIST_LEN);
        if (full) {
            for (uint32_t i = 0; i < BUS_RATE_HIST_LEN; i++) {
                int32_t v = (i < n) ? (int32_t)hist[i] : 0;
                lv_chart_set_series_value_by_id(sc->chart, sc->ser[s], i, v);
            }
        } else if (n > 0) {
            lv_chart_set_next_value(sc->chart, sc->ser[s], (int32_t)hist[n - 1]);
        }
    }
    lv_chart_refresh(sc->chart);
}
