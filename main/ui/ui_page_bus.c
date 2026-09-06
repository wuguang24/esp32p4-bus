/**
 * @file ui_page_bus.c
 * @brief CAN / RS485：过滤 + Δt 列表 + Compose/重放/周期发
 */

#include "ui_pages.h"
#include "ui_theme.h"
#include "ui_frame_list.h"
#include "ui_numpad.h"
#include "ui_scope.h"
#include "bus_app.h"
#include "bus_capture.h"
#include "bus_decode.h"
#include "bus_framer.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t *root;
    lv_obj_t *kpi;
    lv_obj_t *top;
    lv_obj_t *detail;
    ui_scope_t scope;
    lv_obj_t *compose_lbl;
    lv_obj_t *frm_lbl;
    lv_obj_t *btn_rx;
    lv_obj_t *btn_tx;
    lv_obj_t *btn_err;
    lv_obj_t *btn_period;
    ui_frame_list_t list_can;
    ui_frame_list_t list_485;
    bus_frame_t sel;
    bool have_sel;
    bool hold;
    bool filt_rx;
    bool filt_tx;
    bool filt_err;
    bool filt_id;
    uint32_t filt_id_val;
    uint32_t can_id;
    uint8_t can_data[8];
    uint8_t can_len;
    uint8_t can_edit_i; /* 正在编辑的字节下标 */
    uint8_t can_preset;
    uint8_t rs_data[32];
    uint8_t rs_len;
    uint8_t rs_edit_i;
    uint8_t rs_preset;
    uint8_t period_ms_idx; /* 0=off,1=100,2=500,3=1000 */
    lv_timer_t *period_tmr;
    bus_framer_cfg_t frm;
    uint8_t sof_edit_i;
    uint8_t eof_edit_i;
} bus_page_t;

static bus_page_t s_bp;

static const uint32_t s_period_ms[] = {0, 100, 500, 1000};

static const uint8_t s_can_preset_data[][8] = {
    {0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0},
    {0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0, 0},
    {0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0},
    {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04},
};
static const uint8_t s_can_preset_len[] = {4, 4, 1, 8};

static const uint8_t s_rs_p0[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
static const uint8_t s_rs_p1[] = {0x01, 0x06, 0x00, 0x01, 0x00, 0x0A, 0xD9, 0xC9};
static const uint8_t s_rs_p2[] = {'H', 'I', '\r', '\n'};

static void paint_filt_btns(void);
static void refresh_compose_lbl(void);
static void refresh_framer_lbl(void);

static void load_rs_framer(void)
{
    bus_app_get_rs485_framer(&s_bp.frm);
    if (s_bp.frm.fixed_len < 1) {
        s_bp.frm.fixed_len = 8;
    }
}

static void apply_rs_framer(void)
{
    bus_app_set_rs485_framer(&s_bp.frm);
    refresh_framer_lbl();
}

static void refresh_framer_lbl(void)
{
    if (!s_bp.frm_lbl) {
        return;
    }
    char buf[72];
    const char *mode = "闲";
    if (s_bp.frm.mode == BUS_FR_FIXED) {
        mode = "定";
    } else if (s_bp.frm.mode == BUS_FR_MARK) {
        mode = "帧";
    }
    size_t p = (size_t)snprintf(buf, sizeof(buf), "485 %s L%u", mode,
                                (unsigned)s_bp.frm.fixed_len);
    if (s_bp.frm.sof_n > 0 && p + 8 < sizeof(buf)) {
        p += (size_t)snprintf(buf + p, sizeof(buf) - p, " H");
        for (uint8_t i = 0; i < s_bp.frm.sof_n && p + 3 < sizeof(buf); i++) {
            p += (size_t)snprintf(buf + p, sizeof(buf) - p, "%02X", s_bp.frm.sof[i]);
        }
    }
    if (s_bp.frm.eof_n > 0 && p + 8 < sizeof(buf)) {
        p += (size_t)snprintf(buf + p, sizeof(buf) - p, " T");
        for (uint8_t i = 0; i < s_bp.frm.eof_n && p + 3 < sizeof(buf); i++) {
            p += (size_t)snprintf(buf + p, sizeof(buf) - p, "%02X", s_bp.frm.eof[i]);
        }
    }
    (void)p;
    ui_label_set_if_changed(s_bp.frm_lbl, buf);
}

static void fill_filter(bus_capture_filter_t *f)
{
    memset(f, 0, sizeof(*f));
    if (s_bp.filt_rx) {
        f->flags |= BUS_FILT_RX;
    }
    if (s_bp.filt_tx) {
        f->flags |= BUS_FILT_TX;
    }
    if (s_bp.filt_err) {
        f->flags |= BUS_FILT_ERR;
    }
    f->id_en = s_bp.filt_id;
    f->id = s_bp.filt_id_val;
}

static void refresh_compose_lbl(void)
{
    if (!s_bp.compose_lbl) {
        return;
    }
    char buf[112];
    size_t pos = 0;
    pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "C%03lX[%u]",
                            (unsigned long)s_bp.can_id, (unsigned)s_bp.can_edit_i);
    for (uint8_t i = 0; i < s_bp.can_len && pos + 3 < sizeof(buf); i++) {
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "%s%02X",
                                (i == 0) ? " " : ((i == s_bp.can_edit_i) ? ">" : " "),
                                s_bp.can_data[i]);
    }
    pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, " R%u",
                            (unsigned)s_bp.rs_len);
    for (uint8_t i = 0; i < s_bp.rs_len && i < 6 && pos + 3 < sizeof(buf); i++) {
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, " %02X", s_bp.rs_data[i]);
    }
    if (s_bp.rs_len > 6 && pos + 4 < sizeof(buf)) {
        snprintf(buf + pos, sizeof(buf) - pos, "..");
    } else {
        (void)pos;
    }
    ui_label_set_if_changed(s_bp.compose_lbl, buf);
}

static void apply_can_preset(uint8_t idx)
{
    if (idx >= sizeof(s_can_preset_len)) {
        idx = 0;
    }
    s_bp.can_preset = idx;
    s_bp.can_len = s_can_preset_len[idx];
    memcpy(s_bp.can_data, s_can_preset_data[idx], s_bp.can_len);
    if (s_bp.can_edit_i >= s_bp.can_len && s_bp.can_len > 0) {
        s_bp.can_edit_i = (uint8_t)(s_bp.can_len - 1);
    }
    refresh_compose_lbl();
}

static void apply_rs_preset(uint8_t idx)
{
    const uint8_t *p = s_rs_p0;
    size_t n = sizeof(s_rs_p0);
    s_bp.rs_preset = idx % 3;
    if (s_bp.rs_preset == 1) {
        p = s_rs_p1;
        n = sizeof(s_rs_p1);
    } else if (s_bp.rs_preset == 2) {
        p = s_rs_p2;
        n = sizeof(s_rs_p2);
    }
    if (n > sizeof(s_bp.rs_data)) {
        n = sizeof(s_bp.rs_data);
    }
    memcpy(s_bp.rs_data, p, n);
    s_bp.rs_len = (uint8_t)n;
    s_bp.rs_edit_i = 0;
    refresh_compose_lbl();
}

static void paint_filt_btns(void)
{
    if (s_bp.btn_rx) {
        ui_style_chip_btn(s_bp.btn_rx, s_bp.filt_rx);
    }
    if (s_bp.btn_tx) {
        ui_style_chip_btn(s_bp.btn_tx, s_bp.filt_tx);
    }
    if (s_bp.btn_err) {
        ui_style_chip_btn(s_bp.btn_err, s_bp.filt_err);
    }
    if (s_bp.btn_period) {
        char buf[16];
        if (s_bp.period_ms_idx == 0) {
            snprintf(buf, sizeof(buf), "周期");
        } else {
            snprintf(buf, sizeof(buf), "%lums", (unsigned long)s_period_ms[s_bp.period_ms_idx]);
        }
        lv_obj_t *lb = lv_obj_get_child(s_bp.btn_period, 0);
        if (lb) {
            lv_label_set_text(lb, buf);
        }
        ui_style_chip_btn(s_bp.btn_period, s_bp.period_ms_idx != 0);
    }
}

static void on_hold(lv_event_t *e)
{
    s_bp.hold = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

static void on_clear(lv_event_t *e)
{
    (void)e;
    bus_app_clear_capture();
    ui_frame_list_clear(&s_bp.list_can);
    ui_frame_list_clear(&s_bp.list_485);
    if (s_bp.detail) {
        lv_label_set_text(s_bp.detail, "点选帧查看解码");
    }
}

static void on_select_frame(const bus_frame_t *f, void *user)
{
    (void)user;
    if (!f) {
        return;
    }
    s_bp.sel = *f;
    s_bp.have_sel = true;
    if (f->src == BUS_SRC_CAN) {
        s_bp.can_id = f->id;
        s_bp.can_len = f->len > 8 ? 8 : f->len;
        memcpy(s_bp.can_data, f->data, s_bp.can_len);
        s_bp.can_edit_i = 0;
        refresh_compose_lbl();
    } else if (f->src == BUS_SRC_RS485) {
        s_bp.rs_len = f->len > sizeof(s_bp.rs_data) ? sizeof(s_bp.rs_data) : f->len;
        memcpy(s_bp.rs_data, f->data, s_bp.rs_len);
        s_bp.rs_edit_i = 0;
        refresh_compose_lbl();
    }
    if (s_bp.detail) {
        char buf[192];
        bus_decode_format_detail(f, buf, sizeof(buf));
        ui_label_set_if_changed(s_bp.detail, buf);
    }
}

static void on_can_id_done(uint32_t value, void *user_data)
{
    (void)user_data;
    s_bp.can_id = value & 0x1FFFFFFFu;
    refresh_compose_lbl();
}

static void on_filt_id_done(uint32_t value, void *user_data)
{
    (void)user_data;
    s_bp.filt_id_val = value & 0x1FFFFFFFu;
    s_bp.filt_id = true;
    bool ext = s_bp.filt_id_val > 0x7FFu;
    uint32_t mask = ext ? 0x1FFFFFFFu : 0x7FFu;
    (void)bus_app_set_can_filter(true, s_bp.filt_id_val, mask, ext);
    if (s_bp.detail) {
        char buf[64];
        snprintf(buf, sizeof(buf), "软+硬件滤波 ID=0x%lX", (unsigned long)s_bp.filt_id_val);
        ui_label_set_if_changed(s_bp.detail, buf);
    }
}

static void on_id_btn(lv_event_t *e)
{
    (void)e;
    ui_numpad_open("CAN ID", "0~536870911", s_bp.can_id, 0, 0x1FFFFFFF,
                   on_can_id_done, NULL);
}

static void on_data_btn(lv_event_t *e)
{
    (void)e;
    apply_can_preset((uint8_t)((s_bp.can_preset + 1) % 4));
}

static void on_can_byte_done(uint32_t value, void *user_data)
{
    (void)user_data;
    if (s_bp.can_len == 0) {
        s_bp.can_len = 1;
        s_bp.can_edit_i = 0;
    }
    if (s_bp.can_edit_i >= s_bp.can_len) {
        s_bp.can_edit_i = (uint8_t)(s_bp.can_len - 1);
    }
    s_bp.can_data[s_bp.can_edit_i] = (uint8_t)(value & 0xFFu);
    if (s_bp.can_edit_i + 1 < s_bp.can_len) {
        s_bp.can_edit_i++;
    }
    refresh_compose_lbl();
}

static void on_can_len_done(uint32_t value, void *user_data)
{
    (void)user_data;
    if (value > 8) {
        value = 8;
    }
    s_bp.can_len = (uint8_t)value;
    if (s_bp.can_edit_i >= s_bp.can_len && s_bp.can_len > 0) {
        s_bp.can_edit_i = (uint8_t)(s_bp.can_len - 1);
    } else if (s_bp.can_len == 0) {
        s_bp.can_edit_i = 0;
    }
    refresh_compose_lbl();
}

static void on_byte_btn(lv_event_t *e)
{
    (void)e;
    if (s_bp.can_len == 0) {
        s_bp.can_len = 1;
        s_bp.can_edit_i = 0;
    }
    char title[24];
    snprintf(title, sizeof(title), "CAN B%u", (unsigned)s_bp.can_edit_i);
    ui_numpad_open(title, "0~255 确认后下移",
                   s_bp.can_data[s_bp.can_edit_i], 0, 255,
                   on_can_byte_done, NULL);
}

static void on_len_btn(lv_event_t *e)
{
    (void)e;
    ui_numpad_open("CAN LEN", "0~8", s_bp.can_len, 0, 8, on_can_len_done, NULL);
}

static void on_rs_byte_done(uint32_t value, void *user_data)
{
    (void)user_data;
    if (s_bp.rs_len == 0) {
        s_bp.rs_len = 1;
        s_bp.rs_edit_i = 0;
    }
    if (s_bp.rs_edit_i >= s_bp.rs_len) {
        s_bp.rs_edit_i = (uint8_t)(s_bp.rs_len - 1);
    }
    s_bp.rs_data[s_bp.rs_edit_i] = (uint8_t)(value & 0xFFu);
    if (s_bp.rs_edit_i + 1 < s_bp.rs_len) {
        s_bp.rs_edit_i++;
    }
    refresh_compose_lbl();
}

/* 点选 485 帧可装载；R预 切预设；字 改当前 485 字节 */
static void on_rs_preset_btn(lv_event_t *e)
{
    (void)e;
    apply_rs_preset((uint8_t)((s_bp.rs_preset + 1) % 3));
}

static void on_rs_byte_btn(lv_event_t *e)
{
    (void)e;
    if (s_bp.rs_len == 0) {
        apply_rs_preset(0);
    }
    char title[24];
    snprintf(title, sizeof(title), "485 B%u", (unsigned)s_bp.rs_edit_i);
    ui_numpad_open(title, "0~255", s_bp.rs_data[s_bp.rs_edit_i], 0, 255,
                   on_rs_byte_done, NULL);
}

static void on_send_can(lv_event_t *e)
{
    (void)e;
    bool ext = s_bp.can_id > 0x7FFu;
    esp_err_t err = bus_app_can_send(s_bp.can_id, ext, s_bp.can_data, s_bp.can_len);
    if (s_bp.detail) {
        ui_label_set_if_changed(s_bp.detail,
                                (err == ESP_OK) ? "CAN TX OK"
                                                : "CAN TX 失败：允许发送+关仅听");
    }
}

static void on_send_rs(lv_event_t *e)
{
    (void)e;
    if (s_bp.rs_len == 0) {
        apply_rs_preset(0);
    }
    esp_err_t err = bus_app_rs485_send(s_bp.rs_data, s_bp.rs_len);
    if (s_bp.detail) {
        char buf[48];
        snprintf(buf, sizeof(buf), (err == ESP_OK) ? "485 TX %uB OK" : "485 TX 失败",
                 (unsigned)s_bp.rs_len);
        ui_label_set_if_changed(s_bp.detail, buf);
    }
}

static void on_replay(lv_event_t *e)
{
    (void)e;
    if (!s_bp.have_sel) {
        if (s_bp.detail) {
            ui_label_set_if_changed(s_bp.detail, "先点选一帧再重放");
        }
        return;
    }
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;
    if (s_bp.sel.src == BUS_SRC_CAN) {
        err = bus_app_can_send(s_bp.sel.id, (s_bp.sel.flags & BUS_FLAG_EXT) != 0,
                               s_bp.sel.data, s_bp.sel.len);
    } else if (s_bp.sel.src == BUS_SRC_RS485) {
        err = bus_app_rs485_send(s_bp.sel.data, s_bp.sel.len);
    }
    if (s_bp.detail) {
        ui_label_set_if_changed(s_bp.detail, (err == ESP_OK) ? "重放 OK" : "重放失败");
    }
}

static void period_cb(lv_timer_t *t)
{
    (void)t;
    if (s_bp.period_ms_idx == 0) {
        return;
    }
    bool ext = s_bp.can_id > 0x7FFu;
    (void)bus_app_can_send(s_bp.can_id, ext, s_bp.can_data, s_bp.can_len);
}

static void sync_period_timer(void)
{
    uint32_t ms = s_period_ms[s_bp.period_ms_idx];
    if (ms == 0) {
        if (s_bp.period_tmr) {
            lv_timer_pause(s_bp.period_tmr);
        }
        return;
    }
    if (!s_bp.period_tmr) {
        s_bp.period_tmr = lv_timer_create(period_cb, ms, NULL);
    } else {
        lv_timer_set_period(s_bp.period_tmr, ms);
        lv_timer_resume(s_bp.period_tmr);
    }
}

static void on_period(lv_event_t *e)
{
    (void)e;
    s_bp.period_ms_idx = (uint8_t)((s_bp.period_ms_idx + 1) % 4);
    sync_period_timer();
    paint_filt_btns();
}

static void on_filt_rx(lv_event_t *e)
{
    (void)e;
    s_bp.filt_rx = !s_bp.filt_rx;
    paint_filt_btns();
}

static void on_filt_tx(lv_event_t *e)
{
    (void)e;
    s_bp.filt_tx = !s_bp.filt_tx;
    paint_filt_btns();
}

static void on_filt_err(lv_event_t *e)
{
    (void)e;
    s_bp.filt_err = !s_bp.filt_err;
    paint_filt_btns();
}

static void on_filt_id(lv_event_t *e)
{
    (void)e;
    if (s_bp.filt_id) {
        s_bp.filt_id = false;
        (void)bus_app_set_can_filter(false, 0, 0, false);
        if (s_bp.detail) {
            ui_label_set_if_changed(s_bp.detail, "ID 滤波已关（收全部）");
        }
        return;
    }
    ui_numpad_open("过滤 ID", "再按清除", s_bp.filt_id_val, 0, 0x1FFFFFFF,
                   on_filt_id_done, NULL);
}

static void on_frm_mode(lv_event_t *e)
{
    (void)e;
    s_bp.frm.mode = (bus_framer_mode_t)((s_bp.frm.mode + 1) % 3);
    if (s_bp.frm.mode == BUS_FR_MARK && s_bp.frm.sof_n == 0) {
        s_bp.frm.sof_n = 1;
        s_bp.frm.sof[0] = 0xAA;
    }
    apply_rs_framer();
}

static void on_frm_len_done(uint32_t value, void *user_data)
{
    (void)user_data;
    if (value < 1) {
        value = 1;
    }
    if (value > BUS_RS485_FRAME_MAX) {
        value = BUS_RS485_FRAME_MAX;
    }
    s_bp.frm.fixed_len = (uint8_t)value;
    apply_rs_framer();
}

static void on_frm_len(lv_event_t *e)
{
    (void)e;
    ui_numpad_open("485帧长", "1~64", s_bp.frm.fixed_len, 1, BUS_RS485_FRAME_MAX,
                   on_frm_len_done, NULL);
}

static void on_frm_sof_done(uint32_t value, void *user_data)
{
    (void)user_data;
    if (s_bp.frm.sof_n == 0) {
        s_bp.frm.sof_n = 1;
        s_bp.sof_edit_i = 0;
    }
    if (s_bp.sof_edit_i >= s_bp.frm.sof_n) {
        s_bp.sof_edit_i = (uint8_t)(s_bp.frm.sof_n - 1);
    }
    s_bp.frm.sof[s_bp.sof_edit_i] = (uint8_t)(value & 0xFFu);
    if (s_bp.frm.mode == BUS_FR_IDLE) {
        s_bp.frm.mode = BUS_FR_MARK;
    }
    apply_rs_framer();
}

static void on_frm_sof(lv_event_t *e)
{
    (void)e;
    if (s_bp.frm.sof_n == 0) {
        s_bp.frm.sof_n = 1;
        s_bp.sof_edit_i = 0;
        s_bp.frm.sof[0] = 0xAA;
    } else if (s_bp.sof_edit_i + 1 < s_bp.frm.sof_n) {
        s_bp.sof_edit_i++;
    } else if (s_bp.frm.sof_n < 2) {
        s_bp.frm.sof_n++;
        s_bp.sof_edit_i = (uint8_t)(s_bp.frm.sof_n - 1);
        s_bp.frm.sof[s_bp.sof_edit_i] = 0x00;
        apply_rs_framer();
    } else {
        s_bp.sof_edit_i = 0;
    }
    char title[20];
    snprintf(title, sizeof(title), "SOF[%u]", (unsigned)s_bp.sof_edit_i);
    ui_numpad_open(title, "0~255", s_bp.frm.sof[s_bp.sof_edit_i], 0, 255,
                   on_frm_sof_done, NULL);
}

static void on_frm_eof_done(uint32_t value, void *user_data)
{
    (void)user_data;
    if (s_bp.frm.eof_n == 0) {
        s_bp.frm.eof_n = 1;
        s_bp.eof_edit_i = 0;
    }
    if (s_bp.eof_edit_i >= s_bp.frm.eof_n) {
        s_bp.eof_edit_i = (uint8_t)(s_bp.frm.eof_n - 1);
    }
    s_bp.frm.eof[s_bp.eof_edit_i] = (uint8_t)(value & 0xFFu);
    if (s_bp.frm.mode == BUS_FR_IDLE) {
        s_bp.frm.mode = BUS_FR_MARK;
    }
    apply_rs_framer();
}

static void on_frm_eof(lv_event_t *e)
{
    (void)e;
    if (s_bp.frm.eof_n == 0) {
        s_bp.frm.eof_n = 1;
        s_bp.eof_edit_i = 0;
        s_bp.frm.eof[0] = 0x55;
    } else if (s_bp.eof_edit_i + 1 < s_bp.frm.eof_n) {
        s_bp.eof_edit_i++;
    } else if (s_bp.frm.eof_n < 2) {
        s_bp.frm.eof_n++;
        s_bp.eof_edit_i = (uint8_t)(s_bp.frm.eof_n - 1);
        s_bp.frm.eof[s_bp.eof_edit_i] = 0x00;
        apply_rs_framer();
    } else {
        s_bp.eof_edit_i = 0;
    }
    char title[20];
    snprintf(title, sizeof(title), "EOF[%u]", (unsigned)s_bp.eof_edit_i);
    ui_numpad_open(title, "0~255", s_bp.frm.eof[s_bp.eof_edit_i], 0, 255,
                   on_frm_eof_done, NULL);
}

static void on_frm_clear_marks(lv_event_t *e)
{
    (void)e;
    s_bp.frm.sof_n = 0;
    s_bp.frm.eof_n = 0;
    s_bp.sof_edit_i = 0;
    s_bp.eof_edit_i = 0;
    apply_rs_framer();
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

lv_obj_t *ui_page_bus_create(lv_obj_t *parent)
{
    memset(&s_bp, 0, sizeof(s_bp));
    s_bp.can_id = 0x123;
    apply_can_preset(0);
    apply_rs_preset(0);
    load_rs_framer();

    s_bp.root = lv_obj_create(parent);
    lv_obj_set_size(s_bp.root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_bp.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_bp.root, 0, 0);
    lv_obj_set_style_pad_all(s_bp.root, 3, 0);
    lv_obj_set_flex_flow(s_bp.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_bp.root, 2, 0);
    lv_obj_remove_flag(s_bp.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bar = lv_obj_create(s_bp.root);
    lv_obj_set_size(bar, lv_pct(100), 24);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 4, 0);

    s_bp.kpi = lv_label_create(bar);
    lv_obj_set_flex_grow(s_bp.kpi, 1);
    lv_obj_set_style_text_font(s_bp.kpi, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_bp.kpi, ui_color(UI_COL_ACCENT), 0);
    lv_label_set_long_mode(s_bp.kpi, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_bp.kpi, "CAN / 485");

    s_bp.top = lv_label_create(s_bp.root);
    lv_obj_set_width(s_bp.top, lv_pct(100));
    lv_label_set_long_mode(s_bp.top, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_bp.top, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_bp.top, ui_color(UI_COL_STEEL), 0);
    lv_label_set_text(s_bp.top, "TOP —");

    lv_obj_t *hold_l = lv_label_create(bar);
    lv_label_set_text(hold_l, "暂停");
    lv_obj_set_style_text_font(hold_l, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(hold_l, ui_color(UI_COL_TEXT_DIM), 0);
    lv_obj_t *hold = lv_switch_create(bar);
    lv_obj_add_event_cb(hold, on_hold, LV_EVENT_VALUE_CHANGED, NULL);
    make_chip(bar, "清空", 44, on_clear, false);

    static const uint8_t scope_srcs[] = {BUS_SRC_CAN, BUS_SRC_RS485};
    ui_scope_init(&s_bp.scope, s_bp.root, 36, scope_srcs, 2);

    lv_obj_t *filt = lv_obj_create(s_bp.root);
    lv_obj_set_size(filt, lv_pct(100), 24);
    lv_obj_set_style_bg_opa(filt, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filt, 0, 0);
    lv_obj_set_style_pad_all(filt, 0, 0);
    lv_obj_set_flex_flow(filt, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filt, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(filt, 3, 0);
    s_bp.btn_rx = make_chip(filt, "RX", 36, on_filt_rx, false);
    s_bp.btn_tx = make_chip(filt, "TX", 36, on_filt_tx, false);
    s_bp.btn_err = make_chip(filt, "ERR", 40, on_filt_err, false);
    make_chip(filt, "ID", 36, on_filt_id, false);
    make_chip(filt, "重放", 44, on_replay, true);
    s_bp.btn_period = make_chip(filt, "周期", 48, on_period, false);
    paint_filt_btns();

    lv_obj_t *comp = lv_obj_create(s_bp.root);
    lv_obj_set_size(comp, lv_pct(100), 24);
    lv_obj_set_style_bg_color(comp, ui_color(UI_COL_PANEL2), 0);
    lv_obj_set_style_bg_opa(comp, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(comp, ui_color(UI_COL_BORDER), 0);
    lv_obj_set_style_border_width(comp, 1, 0);
    lv_obj_set_style_radius(comp, 3, 0);
    lv_obj_set_style_pad_hor(comp, 3, 0);
    lv_obj_set_style_pad_ver(comp, 0, 0);
    lv_obj_set_flex_flow(comp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(comp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(comp, 3, 0);

    s_bp.compose_lbl = lv_label_create(comp);
    lv_obj_set_flex_grow(s_bp.compose_lbl, 1);
    lv_obj_set_style_text_font(s_bp.compose_lbl, UI_FONT_NUM14, 0);
    lv_obj_set_style_text_color(s_bp.compose_lbl, ui_color(UI_COL_TX), 0);
    lv_label_set_long_mode(s_bp.compose_lbl, LV_LABEL_LONG_CLIP);
    refresh_compose_lbl();
    make_chip(comp, "ID", 28, on_id_btn, false);
    make_chip(comp, "预", 28, on_data_btn, false);
    make_chip(comp, "字", 28, on_byte_btn, false);
    make_chip(comp, "长", 28, on_len_btn, false);
    make_chip(comp, "R预", 32, on_rs_preset_btn, false);
    make_chip(comp, "R字", 32, on_rs_byte_btn, false);
    make_chip(comp, "CAN", 36, on_send_can, true);
    make_chip(comp, "485", 36, on_send_rs, true);

    lv_obj_t *frm = lv_obj_create(s_bp.root);
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

    s_bp.frm_lbl = lv_label_create(frm);
    lv_obj_set_flex_grow(s_bp.frm_lbl, 1);
    lv_obj_set_style_text_font(s_bp.frm_lbl, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_bp.frm_lbl, ui_color(UI_COL_STEEL), 0);
    lv_label_set_long_mode(s_bp.frm_lbl, LV_LABEL_LONG_CLIP);
    refresh_framer_lbl();
    make_chip(frm, "闲", 32, on_frm_mode, false);
    make_chip(frm, "长", 32, on_frm_len, false);
    make_chip(frm, "H", 28, on_frm_sof, false);
    make_chip(frm, "T", 28, on_frm_eof, false);
    make_chip(frm, "清", 28, on_frm_clear_marks, false);

    s_bp.detail = lv_label_create(s_bp.root);
    lv_obj_set_width(s_bp.detail, lv_pct(100));
    lv_obj_set_height(s_bp.detail, 32);
    lv_label_set_long_mode(s_bp.detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_bp.detail, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_bp.detail, ui_color(UI_COL_STEEL), 0);
    lv_obj_set_style_bg_color(s_bp.detail, ui_color(UI_COL_SCOPE_FACE), 0);
    lv_obj_set_style_bg_opa(s_bp.detail, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_bp.detail, 2, 0);
    lv_obj_set_style_radius(s_bp.detail, 3, 0);
    lv_label_set_text(s_bp.detail, "点选帧查看解码");

    lv_obj_t *tv = lv_tabview_create(s_bp.root);
    lv_obj_set_flex_grow(tv, 1);
    lv_obj_set_width(tv, lv_pct(100));
    lv_obj_t *tab_c = lv_tabview_add_tab(tv, "CAN");
    lv_obj_t *tab_r = lv_tabview_add_tab(tv, "485");
    ui_style_tabview(tv, 28);
    lv_obj_set_style_pad_all(tab_c, 0, 0);
    lv_obj_set_style_pad_all(tab_r, 0, 0);

    ui_frame_list_init(&s_bp.list_can, tab_c);
    ui_frame_list_init(&s_bp.list_485, tab_r);
    ui_frame_list_set_select_cb(&s_bp.list_can, on_select_frame, NULL);
    ui_frame_list_set_select_cb(&s_bp.list_485, on_select_frame, NULL);
    return s_bp.root;
}

void ui_page_bus_update(void)
{
    if (!s_bp.kpi) {
        return;
    }
    static uint8_t s_div;
    s_div++;
    bus_app_status_t st;
    bus_app_get_status(&st);
    char buf[128];
    const char *stname = "活";
    if (st.can.state == 1) {
        stname = "警";
    } else if (st.can.state == 2) {
        stname = "被";
    } else if (st.can.state == 3) {
        stname = "关";
    }
    if (st.can.last_err[0]) {
        snprintf(buf, sizeof(buf),
                 "T%u R%u %s E%lu %s C%.0f %s",
                 (unsigned)st.can.tec, (unsigned)st.can.rec, stname,
                 (unsigned long)st.can.bus_err,
                 st.can.filter_en ? "滤" : "全",
                 (double)st.ch[BUS_SRC_CAN].fps,
                 st.can.last_err);
    } else {
        snprintf(buf, sizeof(buf),
                 "T%u R%u %s E%lu A%lu/%lu/%lu/%lu/%lu %s C%.0f",
                 (unsigned)st.can.tec, (unsigned)st.can.rec, stname,
                 (unsigned long)st.can.bus_err,
                 (unsigned long)st.can.err_arb,
                 (unsigned long)st.can.err_bit,
                 (unsigned long)st.can.err_form,
                 (unsigned long)st.can.err_stuff,
                 (unsigned long)st.can.err_ack,
                 st.can.filter_en ? "滤" : "全",
                 (double)st.ch[BUS_SRC_CAN].fps);
    }
    ui_label_set_if_changed(s_bp.kpi, buf);

    if (s_bp.top) {
        bus_id_stat_t topc[BUS_TOP_ID_N];
        bus_id_stat_t topr[BUS_TOP_ID_N];
        size_t nc = bus_capture_get_top_ids(BUS_SRC_CAN, topc, BUS_TOP_ID_N);
        size_t nr = bus_capture_get_top_ids(BUS_SRC_RS485, topr, 3);
        char tbuf[128];
        size_t p = 0;
        p += (size_t)snprintf(tbuf + p, sizeof(tbuf) - p, "CAN ");
        if (nc == 0) {
            p += (size_t)snprintf(tbuf + p, sizeof(tbuf) - p, "- ");
        }
        for (size_t i = 0; i < nc && p + 12 < sizeof(tbuf); i++) {
            p += (size_t)snprintf(tbuf + p, sizeof(tbuf) - p, "%lX:%lu ",
                                  (unsigned long)topc[i].id,
                                  (unsigned long)topc[i].count);
        }
        p += (size_t)snprintf(tbuf + p, sizeof(tbuf) - p, "485 ");
        if (nr == 0) {
            snprintf(tbuf + p, sizeof(tbuf) - p, "-");
        }
        for (size_t i = 0; i < nr && p + 12 < sizeof(tbuf); i++) {
            p += (size_t)snprintf(tbuf + p, sizeof(tbuf) - p, "%02lX:%lu ",
                                  (unsigned long)(topr[i].id & 0xFF),
                                  (unsigned long)topr[i].count);
        }
        ui_label_set_if_changed(s_bp.top, tbuf);
    }

    ui_scope_update_load(&s_bp.scope);

    if (s_bp.hold || (s_div & 1u)) {
        return;
    }
    bus_capture_filter_t filt;
    fill_filter(&filt);
    bus_frame_t frames[BUS_LIST_ROWS];
    size_t n = bus_capture_snapshot_src_f(BUS_SRC_CAN, frames, BUS_LIST_ROWS, &filt);
    ui_frame_list_update(&s_bp.list_can, frames, n, true);
    n = bus_capture_snapshot_src_f(BUS_SRC_RS485, frames, BUS_LIST_ROWS, &filt);
    ui_frame_list_update(&s_bp.list_485, frames, n, true);
}
