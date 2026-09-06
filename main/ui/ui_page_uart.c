/**
 * @file ui_page_uart.c
 * @brief 串口监视：过滤 + Δt + 帧规 + Compose/重放
 */

#include "ui_pages.h"
#include "ui_theme.h"
#include "ui_frame_list.h"
#include "ui_scope.h"
#include "ui_numpad.h"
#include "bus_app.h"
#include "bus_capture.h"
#include "bus_decode.h"
#include "bus_framer.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t *root;
    lv_obj_t *kpi;
    ui_scope_t scope;
    lv_obj_t *detail;
    lv_obj_t *compose_lbl;
    lv_obj_t *frm_lbl;
    lv_obj_t *btn_rx;
    lv_obj_t *btn_tx;
    lv_obj_t *btn_err;
    ui_frame_list_t list;
    bus_frame_t sel;
    bool have_sel;
    bool hold;
    bool filt_rx;
    bool filt_tx;
    bool filt_err;
    uint8_t tx_data[32];
    uint8_t tx_len;
    uint8_t tx_edit_i;
    uint8_t preset;
    bus_framer_cfg_t frm;
    uint8_t sof_edit_i;
    uint8_t eof_edit_i;
} uart_page_t;

static uart_page_t s_up;

static const uint8_t s_u_p0[] = {'A', 'T', '\r', '\n'};
static const uint8_t s_u_p1[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
static const uint8_t s_u_p2[] = {0x55, 0xAA, 0x00, 0xFF};

static void refresh_framer_lbl(void);

static void load_framer(void)
{
    bus_app_get_uart_framer(&s_up.frm);
    if (s_up.frm.fixed_len < 1) {
        s_up.frm.fixed_len = 8;
    }
}

static void apply_framer(void)
{
    bus_app_set_uart_framer(&s_up.frm);
    refresh_framer_lbl();
}

static void refresh_framer_lbl(void)
{
    if (!s_up.frm_lbl) {
        return;
    }
    char buf[72];
    const char *mode = "闲";
    if (s_up.frm.mode == BUS_FR_FIXED) {
        mode = "定";
    } else if (s_up.frm.mode == BUS_FR_MARK) {
        mode = "帧";
    }
    size_t p = (size_t)snprintf(buf, sizeof(buf), "%s L%u", mode,
                                (unsigned)s_up.frm.fixed_len);
    if (s_up.frm.sof_n > 0 && p + 8 < sizeof(buf)) {
        p += (size_t)snprintf(buf + p, sizeof(buf) - p, " H");
        for (uint8_t i = 0; i < s_up.frm.sof_n && p + 3 < sizeof(buf); i++) {
            p += (size_t)snprintf(buf + p, sizeof(buf) - p, "%02X", s_up.frm.sof[i]);
        }
    }
    if (s_up.frm.eof_n > 0 && p + 8 < sizeof(buf)) {
        p += (size_t)snprintf(buf + p, sizeof(buf) - p, " T");
        for (uint8_t i = 0; i < s_up.frm.eof_n && p + 3 < sizeof(buf); i++) {
            p += (size_t)snprintf(buf + p, sizeof(buf) - p, "%02X", s_up.frm.eof[i]);
        }
    }
    (void)p;
    ui_label_set_if_changed(s_up.frm_lbl, buf);
}

static void apply_uart_preset(uint8_t idx)
{
    const uint8_t *p = s_u_p0;
    size_t n = sizeof(s_u_p0);
    s_up.preset = idx % 3;
    if (s_up.preset == 1) {
        p = s_u_p1;
        n = sizeof(s_u_p1);
    } else if (s_up.preset == 2) {
        p = s_u_p2;
        n = sizeof(s_u_p2);
    }
    if (n > sizeof(s_up.tx_data)) {
        n = sizeof(s_up.tx_data);
    }
    memcpy(s_up.tx_data, p, n);
    s_up.tx_len = (uint8_t)n;
    s_up.tx_edit_i = 0;
}

static void paint_filt(void)
{
    if (s_up.btn_rx) {
        ui_style_chip_btn(s_up.btn_rx, s_up.filt_rx);
    }
    if (s_up.btn_tx) {
        ui_style_chip_btn(s_up.btn_tx, s_up.filt_tx);
    }
    if (s_up.btn_err) {
        ui_style_chip_btn(s_up.btn_err, s_up.filt_err);
    }
}

static void refresh_compose(void)
{
    if (!s_up.compose_lbl) {
        return;
    }
    char buf[80];
    size_t pos = 0;
    pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "TX[%u]",
                            (unsigned)s_up.tx_edit_i);
    for (uint8_t i = 0; i < s_up.tx_len && i < 8 && pos + 3 < sizeof(buf); i++) {
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, " %02X", s_up.tx_data[i]);
    }
    if (s_up.tx_len > 8) {
        snprintf(buf + pos, sizeof(buf) - pos, "..");
    }
    ui_label_set_if_changed(s_up.compose_lbl, buf);
}

static void on_hold(lv_event_t *e)
{
    s_up.hold = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

static void on_clear(lv_event_t *e)
{
    (void)e;
    bus_app_clear_capture();
    ui_frame_list_clear(&s_up.list);
    if (s_up.detail) {
        lv_label_set_text(s_up.detail, "点选帧查看解码");
    }
}

static void on_select_frame(const bus_frame_t *f, void *user)
{
    (void)user;
    if (!f) {
        return;
    }
    s_up.sel = *f;
    s_up.have_sel = true;
    if (f->src == BUS_SRC_UART) {
        s_up.tx_len = f->len > sizeof(s_up.tx_data) ? sizeof(s_up.tx_data) : f->len;
        memcpy(s_up.tx_data, f->data, s_up.tx_len);
        s_up.tx_edit_i = 0;
        refresh_compose();
    }
    if (s_up.detail) {
        char buf[192];
        bus_decode_format_detail(f, buf, sizeof(buf));
        ui_label_set_if_changed(s_up.detail, buf);
    }
}

static void on_cycle_preset(lv_event_t *e)
{
    (void)e;
    apply_uart_preset((uint8_t)((s_up.preset + 1) % 3));
    refresh_compose();
}

static void on_uart_byte_done(uint32_t value, void *user_data)
{
    (void)user_data;
    if (s_up.tx_len == 0) {
        s_up.tx_len = 1;
        s_up.tx_edit_i = 0;
    }
    if (s_up.tx_edit_i >= s_up.tx_len) {
        s_up.tx_edit_i = (uint8_t)(s_up.tx_len - 1);
    }
    s_up.tx_data[s_up.tx_edit_i] = (uint8_t)(value & 0xFFu);
    if (s_up.tx_edit_i + 1 < s_up.tx_len) {
        s_up.tx_edit_i++;
    }
    refresh_compose();
}

static void on_byte_btn(lv_event_t *e)
{
    (void)e;
    if (s_up.tx_len == 0) {
        apply_uart_preset(0);
    }
    char title[24];
    snprintf(title, sizeof(title), "UART B%u", (unsigned)s_up.tx_edit_i);
    ui_numpad_open(title, "0~255", s_up.tx_data[s_up.tx_edit_i], 0, 255,
                   on_uart_byte_done, NULL);
}

static void on_send(lv_event_t *e)
{
    (void)e;
    if (s_up.tx_len == 0) {
        apply_uart_preset(0);
    }
    esp_err_t err = bus_app_uart_send(s_up.tx_data, s_up.tx_len);
    if (s_up.detail) {
        ui_label_set_if_changed(s_up.detail,
                                (err == ESP_OK) ? "UART TX OK" : "UART TX 失败");
    }
}

static void on_replay(lv_event_t *e)
{
    (void)e;
    if (!s_up.have_sel || s_up.sel.src != BUS_SRC_UART) {
        if (s_up.detail) {
            ui_label_set_if_changed(s_up.detail, "先点选 UART 帧");
        }
        return;
    }
    esp_err_t err = bus_app_uart_send(s_up.sel.data, s_up.sel.len);
    if (s_up.detail) {
        ui_label_set_if_changed(s_up.detail, (err == ESP_OK) ? "重放 OK" : "重放失败");
    }
}

static void on_filt_rx(lv_event_t *e)
{
    (void)e;
    s_up.filt_rx = !s_up.filt_rx;
    paint_filt();
}

static void on_filt_tx(lv_event_t *e)
{
    (void)e;
    s_up.filt_tx = !s_up.filt_tx;
    paint_filt();
}

static void on_filt_err(lv_event_t *e)
{
    (void)e;
    s_up.filt_err = !s_up.filt_err;
    paint_filt();
}

static void on_frm_mode(lv_event_t *e)
{
    (void)e;
    s_up.frm.mode = (bus_framer_mode_t)((s_up.frm.mode + 1) % 3);
    if (s_up.frm.mode == BUS_FR_MARK && s_up.frm.sof_n == 0) {
        s_up.frm.sof_n = 1;
        s_up.frm.sof[0] = 0xAA;
    }
    apply_framer();
}

static void on_frm_len_done(uint32_t value, void *user_data)
{
    (void)user_data;
    if (value < 1) {
        value = 1;
    }
    if (value > BUS_UART_FRAME_MAX) {
        value = BUS_UART_FRAME_MAX;
    }
    s_up.frm.fixed_len = (uint8_t)value;
    apply_framer();
}

static void on_frm_len(lv_event_t *e)
{
    (void)e;
    ui_numpad_open("帧长", "1~64", s_up.frm.fixed_len, 1, BUS_UART_FRAME_MAX,
                   on_frm_len_done, NULL);
}

static void on_frm_sof_done(uint32_t value, void *user_data)
{
    (void)user_data;
    if (s_up.frm.sof_n == 0) {
        s_up.frm.sof_n = 1;
        s_up.sof_edit_i = 0;
    }
    if (s_up.sof_edit_i >= s_up.frm.sof_n) {
        s_up.sof_edit_i = (uint8_t)(s_up.frm.sof_n - 1);
    }
    s_up.frm.sof[s_up.sof_edit_i] = (uint8_t)(value & 0xFFu);
    if (s_up.frm.mode == BUS_FR_IDLE) {
        s_up.frm.mode = BUS_FR_MARK;
    }
    apply_framer();
}

static void on_frm_sof(lv_event_t *e)
{
    (void)e;
    if (s_up.frm.sof_n == 0) {
        s_up.frm.sof_n = 1;
        s_up.sof_edit_i = 0;
        s_up.frm.sof[0] = 0xAA;
    } else if (s_up.sof_edit_i + 1 < s_up.frm.sof_n) {
        s_up.sof_edit_i++;
    } else if (s_up.frm.sof_n < 2) {
        s_up.frm.sof_n++;
        s_up.sof_edit_i = (uint8_t)(s_up.frm.sof_n - 1);
        s_up.frm.sof[s_up.sof_edit_i] = 0x00;
        apply_framer();
    } else {
        s_up.sof_edit_i = 0;
    }
    char title[20];
    snprintf(title, sizeof(title), "SOF[%u]", (unsigned)s_up.sof_edit_i);
    ui_numpad_open(title, "0~255", s_up.frm.sof[s_up.sof_edit_i], 0, 255,
                   on_frm_sof_done, NULL);
}

static void on_frm_eof_done(uint32_t value, void *user_data)
{
    (void)user_data;
    if (s_up.frm.eof_n == 0) {
        s_up.frm.eof_n = 1;
        s_up.eof_edit_i = 0;
    }
    if (s_up.eof_edit_i >= s_up.frm.eof_n) {
        s_up.eof_edit_i = (uint8_t)(s_up.frm.eof_n - 1);
    }
    s_up.frm.eof[s_up.eof_edit_i] = (uint8_t)(value & 0xFFu);
    if (s_up.frm.mode == BUS_FR_IDLE) {
        s_up.frm.mode = BUS_FR_MARK;
    }
    apply_framer();
}

static void on_frm_eof(lv_event_t *e)
{
    (void)e;
    if (s_up.frm.eof_n == 0) {
        s_up.frm.eof_n = 1;
        s_up.eof_edit_i = 0;
        s_up.frm.eof[0] = 0x55;
    } else if (s_up.eof_edit_i + 1 < s_up.frm.eof_n) {
        s_up.eof_edit_i++;
    } else if (s_up.frm.eof_n < 2) {
        s_up.frm.eof_n++;
        s_up.eof_edit_i = (uint8_t)(s_up.frm.eof_n - 1);
        s_up.frm.eof[s_up.eof_edit_i] = 0x00;
        apply_framer();
    } else {
        s_up.eof_edit_i = 0;
    }
    char title[20];
    snprintf(title, sizeof(title), "EOF[%u]", (unsigned)s_up.eof_edit_i);
    ui_numpad_open(title, "0~255", s_up.frm.eof[s_up.eof_edit_i], 0, 255,
                   on_frm_eof_done, NULL);
}

static void on_frm_clear_marks(lv_event_t *e)
{
    (void)e;
    s_up.frm.sof_n = 0;
    s_up.frm.eof_n = 0;
    s_up.sof_edit_i = 0;
    s_up.eof_edit_i = 0;
    apply_framer();
}

static lv_obj_t *make_chip(lv_obj_t *parent, const char *txt, int32_t w,
                           lv_event_cb_t cb, bool primary)
{
    lv_obj_t *btn = lv_button_create(parent);
    ui_style_chip_btn(btn, primary);
    lv_obj_set_size(btn, w, 22);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb, txt);
    lv_obj_set_style_text_font(lb, UI_FONT_CN, 0);
    ui_btn_label_layout(btn, lb);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

lv_obj_t *ui_page_uart_create(lv_obj_t *parent)
{
    memset(&s_up, 0, sizeof(s_up));
    apply_uart_preset(0);
    load_framer();

    s_up.root = lv_obj_create(parent);
    lv_obj_set_size(s_up.root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_up.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_up.root, 0, 0);
    lv_obj_set_style_pad_all(s_up.root, 3, 0);
    lv_obj_set_flex_flow(s_up.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_up.root, 2, 0);
    lv_obj_remove_flag(s_up.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bar = lv_obj_create(s_up.root);
    lv_obj_set_size(bar, lv_pct(100), 24);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 4, 0);

    s_up.kpi = lv_label_create(bar);
    lv_obj_set_flex_grow(s_up.kpi, 1);
    lv_obj_set_style_text_font(s_up.kpi, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_up.kpi, ui_color(UI_COL_ACCENT), 0);
    lv_label_set_long_mode(s_up.kpi, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_up.kpi, "串口");

    lv_obj_t *hold_l = lv_label_create(bar);
    lv_label_set_text(hold_l, "暂停");
    lv_obj_set_style_text_font(hold_l, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(hold_l, ui_color(UI_COL_TEXT_DIM), 0);
    lv_obj_t *hold = lv_switch_create(bar);
    lv_obj_add_event_cb(hold, on_hold, LV_EVENT_VALUE_CHANGED, NULL);
    make_chip(bar, "清空", 44, on_clear, false);

    static const uint8_t scope_srcs[] = {BUS_SRC_UART};
    ui_scope_init(&s_up.scope, s_up.root, 32, scope_srcs, 1);

    lv_obj_t *filt = lv_obj_create(s_up.root);
    lv_obj_set_size(filt, lv_pct(100), 24);
    lv_obj_set_style_bg_opa(filt, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filt, 0, 0);
    lv_obj_set_style_pad_all(filt, 0, 0);
    lv_obj_set_flex_flow(filt, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(filt, 3, 0);
    s_up.btn_rx = make_chip(filt, "RX", 36, on_filt_rx, false);
    s_up.btn_tx = make_chip(filt, "TX", 36, on_filt_tx, false);
    s_up.btn_err = make_chip(filt, "ERR", 40, on_filt_err, false);
    make_chip(filt, "重放", 44, on_replay, true);
    paint_filt();

    lv_obj_t *frm = lv_obj_create(s_up.root);
    lv_obj_set_size(frm, lv_pct(100), 24);
    lv_obj_set_style_bg_color(frm, ui_color(UI_COL_PANEL2), 0);
    lv_obj_set_style_bg_opa(frm, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(frm, ui_color(UI_COL_BORDER), 0);
    lv_obj_set_style_border_width(frm, 1, 0);
    lv_obj_set_style_radius(frm, 3, 0);
    lv_obj_set_style_pad_hor(frm, 3, 0);
    lv_obj_set_flex_flow(frm, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(frm, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(frm, 3, 0);

    s_up.frm_lbl = lv_label_create(frm);
    lv_obj_set_flex_grow(s_up.frm_lbl, 1);
    lv_obj_set_style_text_font(s_up.frm_lbl, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_up.frm_lbl, ui_color(UI_COL_STEEL), 0);
    lv_label_set_long_mode(s_up.frm_lbl, LV_LABEL_LONG_CLIP);
    refresh_framer_lbl();
    make_chip(frm, "闲", 32, on_frm_mode, false);
    make_chip(frm, "长", 32, on_frm_len, false);
    make_chip(frm, "H", 28, on_frm_sof, false);
    make_chip(frm, "T", 28, on_frm_eof, false);
    make_chip(frm, "清", 28, on_frm_clear_marks, false);

    lv_obj_t *comp = lv_obj_create(s_up.root);
    lv_obj_set_size(comp, lv_pct(100), 24);
    lv_obj_set_style_bg_color(comp, ui_color(UI_COL_PANEL2), 0);
    lv_obj_set_style_bg_opa(comp, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(comp, ui_color(UI_COL_BORDER), 0);
    lv_obj_set_style_border_width(comp, 1, 0);
    lv_obj_set_style_radius(comp, 3, 0);
    lv_obj_set_style_pad_hor(comp, 3, 0);
    lv_obj_set_flex_flow(comp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(comp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(comp, 3, 0);

    s_up.compose_lbl = lv_label_create(comp);
    lv_obj_set_flex_grow(s_up.compose_lbl, 1);
    lv_obj_set_style_text_font(s_up.compose_lbl, UI_FONT_NUM14, 0);
    lv_obj_set_style_text_color(s_up.compose_lbl, ui_color(UI_COL_TX), 0);
    lv_label_set_long_mode(s_up.compose_lbl, LV_LABEL_LONG_CLIP);
    refresh_compose();
    make_chip(comp, "预", 32, on_cycle_preset, false);
    make_chip(comp, "字", 32, on_byte_btn, false);
    make_chip(comp, "发送", 48, on_send, true);

    s_up.detail = lv_label_create(s_up.root);
    lv_obj_set_width(s_up.detail, lv_pct(100));
    lv_obj_set_height(s_up.detail, 36);
    lv_label_set_long_mode(s_up.detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_up.detail, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_up.detail, ui_color(UI_COL_STEEL), 0);
    lv_obj_set_style_bg_color(s_up.detail, ui_color(UI_COL_SCOPE_FACE), 0);
    lv_obj_set_style_bg_opa(s_up.detail, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_up.detail, 2, 0);
    lv_obj_set_style_radius(s_up.detail, 3, 0);
    lv_label_set_text(s_up.detail, "点选帧查看解码");

    ui_frame_list_init(&s_up.list, s_up.root);
    lv_obj_set_flex_grow(s_up.list.cont, 1);
    ui_frame_list_set_select_cb(&s_up.list, on_select_frame, NULL);
    return s_up.root;
}

void ui_page_uart_update(void)
{
    if (!s_up.kpi) {
        return;
    }
    static uint8_t s_div;
    s_div++;
    bus_app_status_t st;
    bus_app_get_status(&st);
    char buf[72];
    snprintf(buf, sizeof(buf), "串口 %lu  %.0f%%  %.0ffps  D%lu",
             (unsigned long)st.uart_baud,
             (double)st.ch[BUS_SRC_UART].load_pct,
             (double)st.ch[BUS_SRC_UART].fps,
             (unsigned long)st.stats.drop_count);
    ui_label_set_if_changed(s_up.kpi, buf);
    ui_scope_update_load(&s_up.scope);

    if (s_up.hold || (s_div & 1u)) {
        return;
    }
    bus_capture_filter_t filt = {0};
    if (s_up.filt_rx) {
        filt.flags |= BUS_FILT_RX;
    }
    if (s_up.filt_tx) {
        filt.flags |= BUS_FILT_TX;
    }
    if (s_up.filt_err) {
        filt.flags |= BUS_FILT_ERR;
    }
    bus_frame_t frames[BUS_LIST_ROWS];
    size_t n = bus_capture_snapshot_src_f(BUS_SRC_UART, frames, BUS_LIST_ROWS, &filt);
    ui_frame_list_update(&s_up.list, frames, n, true);
}
