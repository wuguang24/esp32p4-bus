/**
 * @file ui_frame_list.c
 */

#include "ui_frame_list.h"
#include "ui_theme.h"
#include "bus_decode.h"
#include <string.h>

static void on_row_click(lv_event_t *e)
{
    ui_frame_list_t *fl = (ui_frame_list_t *)lv_event_get_user_data(e);
    lv_obj_t *row = lv_event_get_target(e);
    if (!fl || !row) {
        return;
    }
    int idx = -1;
    for (int i = 0; i < fl->row_n; i++) {
        if (fl->rows[i] == row) {
            idx = i;
            break;
        }
    }
    if (idx < 0 || (size_t)idx >= fl->cache_n) {
        return;
    }
    fl->selected = idx;
    for (int i = 0; i < fl->row_n; i++) {
        if (!fl->rows[i]) {
            continue;
        }
        uint8_t opa = (i == idx) ? (uint8_t)LV_OPA_30 : 0;
        if (fl->row_sel_opa[i] != opa) {
            fl->row_sel_opa[i] = opa;
            lv_obj_set_style_bg_opa(fl->rows[i], opa ? LV_OPA_30 : LV_OPA_TRANSP, 0);
            if (opa) {
                lv_obj_set_style_bg_color(fl->rows[i], ui_color(UI_COL_ACCENT_GLOW), 0);
            }
        }
    }
    if (fl->on_select) {
        fl->on_select(&fl->cache[idx], fl->on_select_user);
    }
}

void ui_frame_list_init(ui_frame_list_t *fl, lv_obj_t *parent)
{
    memset(fl, 0, sizeof(*fl));
    fl->cont = lv_obj_create(parent);
    lv_obj_set_size(fl->cont, lv_pct(100), lv_pct(100));
    ui_style_scope_face(fl->cont);
    lv_obj_set_flex_flow(fl->cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(fl->cont, 0, 0);
    lv_obj_set_style_pad_all(fl->cont, 3, 0);
    lv_obj_add_flag(fl->cont, LV_OBJ_FLAG_SCROLLABLE);
    fl->follow_tail = true;
    fl->selected = -1;

    for (int i = 0; i < BUS_LIST_ROWS; i++) {
        fl->rows[i] = lv_label_create(fl->cont);
        lv_label_set_text(fl->rows[i], "");
        lv_obj_set_style_text_font(fl->rows[i], UI_FONT_CN, 0);
        lv_obj_set_style_text_color(fl->rows[i], ui_color(UI_COL_RX), 0);
        fl->row_col[i] = UI_COL_RX;
        fl->row_sel_opa[i] = 0;
        lv_obj_set_width(fl->rows[i], lv_pct(100));
        lv_obj_set_style_pad_ver(fl->rows[i], 1, 0);
        lv_obj_set_style_radius(fl->rows[i], 2, 0);
        lv_obj_set_style_bg_opa(fl->rows[i], LV_OPA_TRANSP, 0);
        lv_label_set_long_mode(fl->rows[i], LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(fl->rows[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(fl->rows[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(fl->rows[i], on_row_click, LV_EVENT_CLICKED, fl);
    }
    fl->row_n = BUS_LIST_ROWS;
}

void ui_frame_list_set_select_cb(ui_frame_list_t *fl, ui_frame_list_select_cb_t cb, void *user)
{
    if (!fl) {
        return;
    }
    fl->on_select = cb;
    fl->on_select_user = user;
}

void ui_frame_list_update(ui_frame_list_t *fl, const bus_frame_t *frames, size_t n, bool follow_tail)
{
    if (!fl || !fl->cont) {
        return;
    }
    if (n > (size_t)BUS_LIST_ROWS) {
        n = BUS_LIST_ROWS;
    }
    fl->cache_n = n;
    if (frames && n > 0) {
        memcpy(fl->cache, frames, n * sizeof(bus_frame_t));
    }
    if (fl->selected >= (int)n) {
        fl->selected = -1;
    }

    char line[128];
    uint64_t prev_us = 0;
    for (size_t i = 0; i < (size_t)BUS_LIST_ROWS; i++) {
        if (i < n && frames) {
            bus_decode_format_line(&frames[i], prev_us, line, sizeof(line));
            prev_us = frames[i].t_us;
            ui_label_set_if_changed(fl->rows[i], line);
            lv_obj_remove_flag(fl->rows[i], LV_OBJ_FLAG_HIDDEN);

            uint32_t col = UI_COL_RX;
            if (frames[i].flags & (BUS_FLAG_ERR | BUS_FLAG_CRC_BAD)) {
                col = UI_COL_RED;
            } else if (frames[i].dir == BUS_DIR_TX) {
                col = UI_COL_TX;
            } else if (frames[i].src == BUS_SRC_CAN) {
                col = UI_COL_SCOPE_CH1;
            } else if (frames[i].src == BUS_SRC_RS485) {
                col = UI_COL_SCOPE_CH2;
            } else if (frames[i].src == BUS_SRC_UART) {
                col = UI_COL_SCOPE_CH3;
            }
            if (fl->row_col[i] != col) {
                fl->row_col[i] = col;
                lv_obj_set_style_text_color(fl->rows[i], ui_color(col), 0);
            }
            uint8_t opa = ((int)i == fl->selected) ? (uint8_t)LV_OPA_30 : 0;
            if (fl->row_sel_opa[i] != opa) {
                fl->row_sel_opa[i] = opa;
                lv_obj_set_style_bg_opa(fl->rows[i], opa ? LV_OPA_30 : LV_OPA_TRANSP, 0);
                if (opa) {
                    lv_obj_set_style_bg_color(fl->rows[i], ui_color(UI_COL_ACCENT_GLOW), 0);
                }
            }
        } else {
            lv_obj_add_flag(fl->rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (follow_tail && n > 0) {
        lv_obj_scroll_to_view(fl->rows[n - 1], LV_ANIM_OFF);
    }
}

void ui_frame_list_clear(ui_frame_list_t *fl)
{
    if (!fl) {
        return;
    }
    fl->selected = -1;
    fl->cache_n = 0;
    ui_frame_list_update(fl, NULL, 0, false);
}
