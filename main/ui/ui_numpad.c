/**
 * @file ui_numpad.c
 * @brief LVGL 数字键盘弹窗
 */

#include "ui_numpad.h"
#include "ui_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    lv_obj_t *mask;
    lv_obj_t *panel;
    lv_obj_t *ta;
    lv_obj_t *kb;
    lv_obj_t *lbl_err;
    uint32_t min_v;
    uint32_t max_v;
    ui_numpad_cb_t cb;
    void *user_data;
} numpad_t;

static numpad_t s_np;

static void close_ui(void)
{
    if (s_np.mask) {
        lv_obj_delete(s_np.mask);
    }
    memset(&s_np, 0, sizeof(s_np));
}

void ui_numpad_close(void)
{
    close_ui();
}

bool ui_numpad_is_open(void)
{
    return s_np.mask != NULL;
}

static bool parse_apply(void)
{
    if (!s_np.ta) {
        return false;
    }
    const char *txt = lv_textarea_get_text(s_np.ta);
    if (!txt || txt[0] == '\0') {
        if (s_np.lbl_err) {
            lv_label_set_text(s_np.lbl_err, "空值");
        }
        return false;
    }
    char *end = NULL;
    unsigned long v = strtoul(txt, &end, 10);
    if (end == txt || (end && *end != '\0')) {
        if (s_np.lbl_err) {
            lv_label_set_text(s_np.lbl_err, "无效");
        }
        return false;
    }
    if (v < s_np.min_v || v > s_np.max_v) {
        if (s_np.lbl_err) {
            char buf[48];
            snprintf(buf, sizeof(buf), "超出 %lu~%lu",
                     (unsigned long)s_np.min_v, (unsigned long)s_np.max_v);
            lv_label_set_text(s_np.lbl_err, buf);
        }
        return false;
    }
    ui_numpad_cb_t cb = s_np.cb;
    void *ud = s_np.user_data;
    uint32_t out = (uint32_t)v;
    close_ui();
    if (cb) {
        cb(out, ud);
    }
    return true;
}

static void on_kb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        (void)parse_apply();
    } else if (code == LV_EVENT_CANCEL) {
        close_ui();
    }
}

static void on_ok(lv_event_t *e)
{
    (void)e;
    (void)parse_apply();
}

static void on_cancel(lv_event_t *e)
{
    (void)e;
    close_ui();
}

static void on_mask_click(lv_event_t *e)
{
    /* 点遮罩空白处关闭；点到 panel 会 stop bubbling */
    if (lv_event_get_target(e) == s_np.mask) {
        close_ui();
    }
}

static void on_panel_click(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
}

void ui_numpad_open(const char *title, const char *hint,
                    uint32_t initial, uint32_t min_v, uint32_t max_v,
                    ui_numpad_cb_t cb, void *user_data)
{
    if (s_np.mask) {
        close_ui();
    }
    if (min_v > max_v) {
        uint32_t t = min_v;
        min_v = max_v;
        max_v = t;
    }
    if (initial < min_v) {
        initial = min_v;
    }
    if (initial > max_v) {
        initial = max_v;
    }

    s_np.min_v = min_v;
    s_np.max_v = max_v;
    s_np.cb = cb;
    s_np.user_data = user_data;

    lv_obj_t *scr = lv_layer_top();
    s_np.mask = lv_obj_create(scr);
    lv_obj_set_size(s_np.mask, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_np.mask, ui_color(0x000000), 0);
    lv_obj_set_style_bg_opa(s_np.mask, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_np.mask, 0, 0);
    lv_obj_set_style_radius(s_np.mask, 0, 0);
    lv_obj_set_style_pad_all(s_np.mask, 8, 0);
    lv_obj_add_flag(s_np.mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_np.mask, on_mask_click, LV_EVENT_CLICKED, NULL);

    s_np.panel = lv_obj_create(s_np.mask);
    lv_obj_set_size(s_np.panel, lv_pct(100), lv_pct(100));
    lv_obj_center(s_np.panel);
    ui_style_panel(s_np.panel);
    lv_obj_set_style_bg_color(s_np.panel, ui_color(UI_COL_PANEL), 0);
    lv_obj_set_style_pad_all(s_np.panel, 8, 0);
    lv_obj_set_flex_flow(s_np.panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_np.panel, 6, 0);
    lv_obj_add_flag(s_np.panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_np.panel, on_panel_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ttl = lv_label_create(s_np.panel);
    lv_label_set_text(ttl, title ? title : "请输入");
    lv_obj_set_style_text_font(ttl, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(ttl, ui_color(UI_COL_ACCENT), 0);

    if (hint && hint[0]) {
        lv_obj_t *h = lv_label_create(s_np.panel);
        lv_label_set_text(h, hint);
        lv_obj_set_style_text_font(h, UI_FONT_CN, 0);
        lv_obj_set_style_text_color(h, ui_color(UI_COL_TEXT_DIM), 0);
    }

    s_np.ta = lv_textarea_create(s_np.panel);
    lv_obj_set_width(s_np.ta, lv_pct(100));
    lv_textarea_set_one_line(s_np.ta, true);
    lv_textarea_set_max_length(s_np.ta, 10);
    lv_textarea_set_accepted_chars(s_np.ta, "0123456789");
    lv_obj_set_style_text_font(s_np.ta, UI_FONT_NUM20, 0);
    lv_obj_set_style_bg_color(s_np.ta, ui_color(UI_COL_BG), 0);
    lv_obj_set_style_border_color(s_np.ta, ui_color(UI_COL_ACCENT), 0);
    lv_obj_set_style_text_color(s_np.ta, ui_color(UI_COL_TEXT), 0);
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)initial);
        lv_textarea_set_text(s_np.ta, buf);
    }
    lv_obj_add_state(s_np.ta, LV_STATE_FOCUSED);

    s_np.lbl_err = lv_label_create(s_np.panel);
    lv_label_set_text(s_np.lbl_err, " ");
    lv_obj_set_style_text_font(s_np.lbl_err, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_np.lbl_err, ui_color(UI_COL_RED), 0);

    lv_obj_t *row = lv_obj_create(s_np.panel);
    lv_obj_set_size(row, lv_pct(100), 36);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t *bc = lv_button_create(row);
    ui_style_chip_btn(bc, false);
    lv_obj_set_flex_grow(bc, 1);
    lv_obj_set_height(bc, 34);
    lv_obj_t *lc = lv_label_create(bc);
    lv_label_set_text(lc, "取消");
    lv_obj_set_style_text_font(lc, UI_FONT_CN, 0);
    ui_btn_label_layout(bc, lc);
    lv_obj_add_event_cb(bc, on_cancel, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bo = lv_button_create(row);
    ui_style_chip_btn(bo, true);
    lv_obj_set_flex_grow(bo, 1);
    lv_obj_set_height(bo, 34);
    lv_obj_t *lo = lv_label_create(bo);
    lv_label_set_text(lo, "确定");
    lv_obj_set_style_text_font(lo, UI_FONT_CN, 0);
    ui_btn_label_layout(bo, lo);
    lv_obj_add_event_cb(bo, on_ok, LV_EVENT_CLICKED, NULL);

    s_np.kb = lv_keyboard_create(s_np.panel);
    lv_obj_set_width(s_np.kb, lv_pct(100));
    lv_obj_set_flex_grow(s_np.kb, 1);
    lv_keyboard_set_mode(s_np.kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(s_np.kb, s_np.ta);
    lv_obj_set_style_bg_color(s_np.kb, ui_color(UI_COL_PANEL2), 0);
    lv_obj_add_event_cb(s_np.kb, on_kb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_np.kb, on_kb, LV_EVENT_CANCEL, NULL);
}
