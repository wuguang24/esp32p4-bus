/**
 * @file ui_page_io.c
 * @brief I2C / SPI / PWM / ADC / DIO / 1-Wire — 扩展页
 */

#include "ui_pages.h"
#include "ui_theme.h"
#include "ui_frame_list.h"
#include "ui_scope.h"
#include "bus_i2c.h"
#include "bus_spi.h"
#include "bus_app.h"
#include "bus_capture.h"
#include "bus_tools.h"
#include "bus_dio.h"
#include "bus_onewire.h"
#include "siggen_pwm.h"
#include "bus_adc.h"
#include "ui_numpad.h"
#include <stdio.h>
#include <string.h>

enum {
    IO_TAB_I2C = 0,
    IO_TAB_SPI,
    IO_TAB_PWM,
    IO_TAB_ADC,
    IO_TAB_DIO,
    IO_TAB_OW,
};

typedef struct {
    lv_obj_t *root;
    lv_obj_t *kpi;
    lv_obj_t *tv;
    lv_obj_t *lbl_i2c_hz;
    lv_obj_t *lbl_spi_hz;
    lv_obj_t *lbl_spi_mode;
    ui_frame_list_t list_i2c;
    ui_frame_list_t list_spi;
    ui_scope_t scope_i2c;
    ui_scope_t scope_spi;
    bool hold_i2c;
    bool hold_spi;

    lv_obj_t *lbl_pwm;
    lv_obj_t *btn_freq;
    lv_obj_t *lbl_freq;
    lv_obj_t *btn_duty;
    lv_obj_t *lbl_duty;
    lv_obj_t *sw_pwm;

    lv_obj_t *lbl_adc;
    lv_obj_t *bar_adc;
    lv_obj_t *chart_adc;
    lv_chart_series_t *ser_adc;

    lv_obj_t *lbl_dio;
    lv_obj_t *lbl_ow;

    lv_obj_t *lbl_i2c_cmp;
    lv_obj_t *lbl_spi_cmp;
    uint8_t i2c_addr;
    uint8_t i2c_data[8];
    uint8_t i2c_len;
    uint8_t i2c_edit;
    uint8_t spi_data[8];
    uint8_t spi_len;
    uint8_t spi_edit;
} io_page_t;

static io_page_t s_io;

static void refresh_i2c_cmp(void)
{
    if (!s_io.lbl_i2c_cmp) {
        return;
    }
    char buf[72];
    size_t p = 0;
    p += (size_t)snprintf(buf + p, sizeof(buf) - p, "@%02X L%u",
                          (unsigned)s_io.i2c_addr, (unsigned)s_io.i2c_len);
    for (uint8_t i = 0; i < s_io.i2c_len && p + 3 < sizeof(buf); i++) {
        p += (size_t)snprintf(buf + p, sizeof(buf) - p, " %02X", s_io.i2c_data[i]);
    }
    ui_label_set_if_changed(s_io.lbl_i2c_cmp, buf);
}

static void refresh_spi_cmp(void)
{
    if (!s_io.lbl_spi_cmp) {
        return;
    }
    char buf[72];
    size_t p = 0;
    p += (size_t)snprintf(buf + p, sizeof(buf) - p, "TX L%u", (unsigned)s_io.spi_len);
    for (uint8_t i = 0; i < s_io.spi_len && p + 3 < sizeof(buf); i++) {
        p += (size_t)snprintf(buf + p, sizeof(buf) - p, " %02X", s_io.spi_data[i]);
    }
    ui_label_set_if_changed(s_io.lbl_spi_cmp, buf);
}

static void on_scan(lv_event_t *e)
{
    (void)e;
    uint8_t found[24];
    (void)bus_i2c_scan(found, 24);
}

static void io_set_msg(const char *msg)
{
    if (s_io.kpi && msg) {
        ui_label_set_if_changed(s_io.kpi, msg);
    }
}

static void on_i2c_id(lv_event_t *e)
{
    (void)e;
    char txt[160];
    int n = bus_tools_i2c_identify(txt, sizeof(txt));
    (void)n;
    io_set_msg(txt);
}

static void on_i2c_dump(lv_event_t *e)
{
    (void)e;
    /* 以 compose：址=设备，data[0]=寄存器，长=读取长度 */
    uint8_t len = s_io.i2c_len ? s_io.i2c_len : 8;
    if (len > 16) {
        len = 16;
    }
    uint8_t buf[16];
    uint16_t reg = s_io.i2c_data[0];
    esp_err_t err = bus_tools_i2c_dump(s_io.i2c_addr, reg, 1, buf, len);
    char msg[96];
    if (err != ESP_OK) {
        snprintf(msg, sizeof(msg), "DUMP 失败");
    } else {
        size_t p = (size_t)snprintf(msg, sizeof(msg), "D@%02X:%02X", s_io.i2c_addr, (unsigned)reg);
        for (uint8_t i = 0; i < len && p + 3 < sizeof(msg); i++) {
            p += (size_t)snprintf(msg + p, sizeof(msg) - p, " %02X", buf[i]);
        }
    }
    io_set_msg(msg);
}

static void on_spi_jedec(lv_event_t *e)
{
    (void)e;
    uint8_t id[3];
    char name[56];
    esp_err_t err = bus_tools_spi_jedec(id, name, sizeof(name));
    if (err != ESP_OK) {
        io_set_msg("JEDEC 失败");
    } else {
        io_set_msg(name);
    }
}

static void on_spi_flash_rd(lv_event_t *e)
{
    (void)e;
    uint32_t addr = 0;
    if (s_io.spi_len >= 3) {
        addr = ((uint32_t)s_io.spi_data[0] << 16) | ((uint32_t)s_io.spi_data[1] << 8) |
               s_io.spi_data[2];
    }
    uint8_t buf[16];
    esp_err_t err = bus_tools_spi_flash_read(addr, buf, 16);
    char msg[96];
    if (err != ESP_OK) {
        snprintf(msg, sizeof(msg), "READ 失败");
    } else {
        size_t p = (size_t)snprintf(msg, sizeof(msg), "R%06lX", (unsigned long)addr);
        for (int i = 0; i < 8 && p + 3 < sizeof(msg); i++) {
            p += (size_t)snprintf(msg + p, sizeof(msg) - p, " %02X", buf[i]);
        }
    }
    io_set_msg(msg);
}

static void dio_refresh(void)
{
    if (!s_io.lbl_dio) {
        return;
    }
    char buf[64];
    int v = bus_dio_read();
    const char *m = "IN";
    switch (bus_dio_get_mode()) {
    case BUS_DIO_OUT: m = "OUT"; break;
    case BUS_DIO_IN_PU: m = "PU"; break;
    case BUS_DIO_IN_PD: m = "PD"; break;
    default: break;
    }
    snprintf(buf, sizeof(buf), "GPIO%d %s = %d", (int)bus_dio_get_gpio(), m, v);
    ui_label_set_if_changed(s_io.lbl_dio, buf);
}

static void on_dio_mode(lv_event_t *e)
{
    (void)e;
    bus_dio_mode_t m = (bus_dio_mode_t)((bus_dio_get_mode() + 1) % 4);
    (void)bus_dio_set_mode(m);
    dio_refresh();
}

static void on_dio_read(lv_event_t *e)
{
    (void)e;
    dio_refresh();
}

static void on_dio_hi(lv_event_t *e)
{
    (void)e;
    (void)bus_dio_write(true);
    dio_refresh();
}

static void on_dio_lo(lv_event_t *e)
{
    (void)e;
    (void)bus_dio_write(false);
    dio_refresh();
}

static void on_ow_scan(lv_event_t *e)
{
    (void)e;
    (void)bus_ow_init(bus_dio_get_gpio());
    uint8_t roms[4][8];
    int n = bus_ow_search(roms, 4);
    char msg[96];
    if (n <= 0) {
        snprintf(msg, sizeof(msg), "1W 无设备 GPIO%d", (int)bus_ow_get_gpio());
    } else {
        size_t p = (size_t)snprintf(msg, sizeof(msg), "1W %d:", n);
        for (int i = 0; i < 8 && p + 3 < sizeof(msg); i++) {
            p += (size_t)snprintf(msg + p, sizeof(msg) - p, "%02X", roms[0][i]);
        }
    }
    if (s_io.lbl_ow) {
        ui_label_set_if_changed(s_io.lbl_ow, msg);
    }
    io_set_msg(msg);
}

static void on_spi(lv_event_t *e)
{
    (void)e;
    if (s_io.spi_len == 0) {
        s_io.spi_data[0] = 0x9F;
        s_io.spi_len = 4;
        s_io.spi_edit = 0;
        refresh_spi_cmp();
    }
    uint8_t rx[8] = {0};
    (void)bus_spi_xfer(s_io.spi_data, rx, s_io.spi_len);
}

static void on_i2c_addr_ok(uint32_t v, void *u)
{
    (void)u;
    s_io.i2c_addr = (uint8_t)(v & 0x7Fu);
    refresh_i2c_cmp();
}

static void on_i2c_len_ok(uint32_t v, void *u)
{
    (void)u;
    if (v > 8) {
        v = 8;
    }
    s_io.i2c_len = (uint8_t)v;
    if (s_io.i2c_edit >= s_io.i2c_len && s_io.i2c_len > 0) {
        s_io.i2c_edit = (uint8_t)(s_io.i2c_len - 1);
    }
    refresh_i2c_cmp();
}

static void on_i2c_byte_ok(uint32_t v, void *u)
{
    (void)u;
    if (s_io.i2c_len == 0) {
        s_io.i2c_len = 1;
        s_io.i2c_edit = 0;
    }
    s_io.i2c_data[s_io.i2c_edit] = (uint8_t)(v & 0xFFu);
    if (s_io.i2c_edit + 1 < s_io.i2c_len) {
        s_io.i2c_edit++;
    }
    refresh_i2c_cmp();
}

static void on_i2c_addr(lv_event_t *e)
{
    (void)e;
    ui_numpad_open("I2C ADDR", "0~127", s_io.i2c_addr, 0, 127, on_i2c_addr_ok, NULL);
}

static void on_i2c_len(lv_event_t *e)
{
    (void)e;
    ui_numpad_open("I2C LEN", "0~8", s_io.i2c_len, 0, 8, on_i2c_len_ok, NULL);
}

static void on_i2c_byte(lv_event_t *e)
{
    (void)e;
    if (s_io.i2c_len == 0) {
        s_io.i2c_len = 1;
    }
    char t[20];
    snprintf(t, sizeof(t), "I2C B%u", (unsigned)s_io.i2c_edit);
    ui_numpad_open(t, "0~255", s_io.i2c_data[s_io.i2c_edit], 0, 255, on_i2c_byte_ok, NULL);
}

static void on_i2c_wr(lv_event_t *e)
{
    (void)e;
    if (s_io.i2c_len == 0) {
        return;
    }
    (void)bus_i2c_write(s_io.i2c_addr, s_io.i2c_data, s_io.i2c_len);
}

static void on_i2c_rd(lv_event_t *e)
{
    (void)e;
    uint8_t n = s_io.i2c_len ? s_io.i2c_len : 1;
    uint8_t rd[8] = {0};
    if (bus_i2c_read(s_io.i2c_addr, rd, n) == ESP_OK) {
        memcpy(s_io.i2c_data, rd, n);
        s_io.i2c_len = n;
        refresh_i2c_cmp();
    }
}

static void on_spi_len_ok(uint32_t v, void *u)
{
    (void)u;
    if (v > 8) {
        v = 8;
    }
    if (v == 0) {
        v = 1;
    }
    s_io.spi_len = (uint8_t)v;
    if (s_io.spi_edit >= s_io.spi_len) {
        s_io.spi_edit = (uint8_t)(s_io.spi_len - 1);
    }
    refresh_spi_cmp();
}

static void on_spi_byte_ok(uint32_t v, void *u)
{
    (void)u;
    s_io.spi_data[s_io.spi_edit] = (uint8_t)(v & 0xFFu);
    if (s_io.spi_edit + 1 < s_io.spi_len) {
        s_io.spi_edit++;
    }
    refresh_spi_cmp();
}

static void on_spi_len(lv_event_t *e)
{
    (void)e;
    ui_numpad_open("SPI LEN", "1~8", s_io.spi_len ? s_io.spi_len : 4, 1, 8,
                   on_spi_len_ok, NULL);
}

static void on_spi_byte(lv_event_t *e)
{
    (void)e;
    if (s_io.spi_len == 0) {
        s_io.spi_len = 4;
        s_io.spi_data[0] = 0x9F;
    }
    char t[20];
    snprintf(t, sizeof(t), "SPI B%u", (unsigned)s_io.spi_edit);
    ui_numpad_open(t, "0~255", s_io.spi_data[s_io.spi_edit], 0, 255, on_spi_byte_ok, NULL);
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

static void on_hold_i2c(lv_event_t *e)
{
    s_io.hold_i2c = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

static void on_hold_spi(lv_event_t *e)
{
    s_io.hold_spi = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

static void paint_io_rates(void)
{
    bus_app_status_t st;
    bus_app_get_status(&st);
    char buf[32];
    if (s_io.lbl_i2c_hz) {
        snprintf(buf, sizeof(buf), "I2C %luk", (unsigned long)(st.i2c_hz / 1000u));
        ui_label_set_if_changed(s_io.lbl_i2c_hz, buf);
    }
    if (s_io.lbl_spi_hz) {
        if (st.spi_hz >= 1000000u) {
            snprintf(buf, sizeof(buf), "SPI %.0fM", (double)st.spi_hz / 1e6);
        } else {
            snprintf(buf, sizeof(buf), "SPI %luk", (unsigned long)(st.spi_hz / 1000u));
        }
        ui_label_set_if_changed(s_io.lbl_spi_hz, buf);
    }
    if (s_io.lbl_spi_mode) {
        snprintf(buf, sizeof(buf), "Mode%u", (unsigned)st.spi_mode);
        ui_label_set_if_changed(s_io.lbl_spi_mode, buf);
    }
}

static void on_i2c_hz(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    uint32_t next = (st.i2c_hz >= 400000u) ? 100000u : 400000u;
    (void)bus_app_set_i2c_hz(next);
    paint_io_rates();
}

static void on_spi_hz(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    static const uint32_t tab[] = {100000, 1000000, 4000000, 10000000};
    uint32_t next = tab[0];
    for (size_t i = 0; i < 4; i++) {
        if (tab[i] == st.spi_hz) {
            next = tab[(i + 1) % 4];
            break;
        }
    }
    (void)bus_app_set_spi_hz(next);
    paint_io_rates();
}

static void on_spi_mode(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    (void)bus_app_set_spi_mode((uint8_t)((st.spi_mode + 1) % 4));
    paint_io_rates();
}

static void paint_pwm_btns(void)
{
    char buf[24];
    if (s_io.lbl_freq) {
        uint32_t f = siggen_pwm_get_freq();
        if (f >= 1000) {
            snprintf(buf, sizeof(buf), "%lu kHz", (unsigned long)(f / 1000u));
        } else {
            snprintf(buf, sizeof(buf), "%lu Hz", (unsigned long)f);
        }
        ui_label_set_if_changed(s_io.lbl_freq, buf);
    }
    if (s_io.lbl_duty) {
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)siggen_pwm_get_duty());
        ui_label_set_if_changed(s_io.lbl_duty, buf);
    }
    if (s_io.lbl_pwm) {
        snprintf(buf, sizeof(buf), "PWM  引脚%d  %s",
                 (int)siggen_pwm_get_gpio(),
                 siggen_pwm_is_running() ? "开启" : "关闭");
        ui_label_set_if_changed(s_io.lbl_pwm, buf);
    }
}

static void on_pwm_sw(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (on) {
        (void)siggen_pwm_start();
    } else {
        (void)siggen_pwm_stop();
    }
    paint_pwm_btns();
}

static void on_freq_ok(uint32_t v, void *ud)
{
    (void)ud;
    (void)siggen_pwm_set_freq(v);
    paint_pwm_btns();
}

static void on_duty_ok(uint32_t v, void *ud)
{
    (void)ud;
    (void)siggen_pwm_set_duty((uint8_t)v);
    paint_pwm_btns();
}

static void on_freq(lv_event_t *e)
{
    (void)e;
    char hint[40];
    snprintf(hint, sizeof(hint), "%u ~ %u Hz",
             (unsigned)SIGGEN_PWM_FREQ_MIN_HZ, (unsigned)SIGGEN_PWM_FREQ_MAX_HZ);
    ui_numpad_open("PWM 频率 (Hz)", hint,
                   siggen_pwm_get_freq(),
                   SIGGEN_PWM_FREQ_MIN_HZ, SIGGEN_PWM_FREQ_MAX_HZ,
                   on_freq_ok, NULL);
}

static void on_duty(lv_event_t *e)
{
    (void)e;
    ui_numpad_open("PWM 占空比 (%)", "0 ~ 100",
                   siggen_pwm_get_duty(), 0, 100,
                   on_duty_ok, NULL);
}

static void build_bus_tab(lv_obj_t *tab, const char *btn_txt, bool primary,
                          lv_event_cb_t on_btn, lv_event_cb_t on_hold,
                          ui_frame_list_t *list, ui_scope_t *scope, uint8_t src,
                          bool spi_extra)
{
    lv_obj_set_style_pad_all(tab, 4, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, 3, 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);

    lv_obj_t *tools = lv_obj_create(tab);
    lv_obj_set_size(tools, lv_pct(100), 30);
    lv_obj_set_style_bg_opa(tools, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tools, 0, 0);
    lv_obj_set_style_pad_all(tools, 0, 0);
    lv_obj_set_flex_flow(tools, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tools, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tools, 4, 0);

    lv_obj_t *b = lv_button_create(tools);
    ui_style_chip_btn(b, primary);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, 28);
    lv_obj_t *bl = lv_label_create(b);
    lv_label_set_text(bl, btn_txt);
    lv_obj_set_style_text_font(bl, UI_FONT_CN, 0);
    ui_btn_label_layout(b, bl);
    lv_obj_add_event_cb(b, on_btn, LV_EVENT_CLICKED, NULL);

    if (src == BUS_SRC_I2C) {
        lv_obj_t *hz = lv_button_create(tools);
        ui_style_chip_btn(hz, false);
        lv_obj_set_size(hz, 72, 28);
        s_io.lbl_i2c_hz = lv_label_create(hz);
        lv_obj_set_style_text_font(s_io.lbl_i2c_hz, UI_FONT_NUM14, 0);
        ui_btn_label_layout(hz, s_io.lbl_i2c_hz);
        lv_obj_add_event_cb(hz, on_i2c_hz, LV_EVENT_CLICKED, NULL);
    } else if (spi_extra) {
        lv_obj_t *hz = lv_button_create(tools);
        ui_style_chip_btn(hz, false);
        lv_obj_set_size(hz, 64, 28);
        s_io.lbl_spi_hz = lv_label_create(hz);
        lv_obj_set_style_text_font(s_io.lbl_spi_hz, UI_FONT_NUM14, 0);
        ui_btn_label_layout(hz, s_io.lbl_spi_hz);
        lv_obj_add_event_cb(hz, on_spi_hz, LV_EVENT_CLICKED, NULL);

        lv_obj_t *md = lv_button_create(tools);
        ui_style_chip_btn(md, false);
        lv_obj_set_size(md, 56, 28);
        s_io.lbl_spi_mode = lv_label_create(md);
        lv_obj_set_style_text_font(s_io.lbl_spi_mode, UI_FONT_NUM14, 0);
        ui_btn_label_layout(md, s_io.lbl_spi_mode);
        lv_obj_add_event_cb(md, on_spi_mode, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *hl = lv_label_create(tools);
    lv_label_set_text(hl, "暂停");
    lv_obj_set_style_text_font(hl, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(hl, ui_color(UI_COL_TEXT_DIM), 0);
    lv_obj_t *hs = lv_switch_create(tools);
    lv_obj_add_event_cb(hs, on_hold, LV_EVENT_VALUE_CHANGED, NULL);

    ui_scope_init(scope, tab, 28, &src, 1);

    /* 组帧行：I2C 址/字节/读写；SPI 字节/长度/发 */
    lv_obj_t *comp = lv_obj_create(tab);
    lv_obj_set_size(comp, lv_pct(100), 24);
    lv_obj_set_style_bg_color(comp, ui_color(UI_COL_PANEL2), 0);
    lv_obj_set_style_bg_opa(comp, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(comp, 1, 0);
    lv_obj_set_style_border_color(comp, ui_color(UI_COL_BORDER), 0);
    lv_obj_set_style_radius(comp, 3, 0);
    lv_obj_set_style_pad_hor(comp, 3, 0);
    lv_obj_set_style_pad_ver(comp, 0, 0);
    lv_obj_set_flex_flow(comp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(comp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(comp, 3, 0);

    if (src == BUS_SRC_I2C) {
        s_io.lbl_i2c_cmp = lv_label_create(comp);
        lv_obj_set_flex_grow(s_io.lbl_i2c_cmp, 1);
        lv_obj_set_style_text_font(s_io.lbl_i2c_cmp, UI_FONT_NUM14, 0);
        lv_obj_set_style_text_color(s_io.lbl_i2c_cmp, ui_color(UI_COL_TX), 0);
        lv_label_set_long_mode(s_io.lbl_i2c_cmp, LV_LABEL_LONG_CLIP);
        refresh_i2c_cmp();
        make_chip(comp, "址", 28, on_i2c_addr, false);
        make_chip(comp, "字", 28, on_i2c_byte, false);
        make_chip(comp, "长", 28, on_i2c_len, false);
        make_chip(comp, "写", 28, on_i2c_wr, true);
        make_chip(comp, "读", 28, on_i2c_rd, true);
        make_chip(comp, "ID", 28, on_i2c_id, false);
        make_chip(comp, "DUMP", 44, on_i2c_dump, true);
    } else if (src == BUS_SRC_SPI) {
        s_io.lbl_spi_cmp = lv_label_create(comp);
        lv_obj_set_flex_grow(s_io.lbl_spi_cmp, 1);
        lv_obj_set_style_text_font(s_io.lbl_spi_cmp, UI_FONT_NUM14, 0);
        lv_obj_set_style_text_color(s_io.lbl_spi_cmp, ui_color(UI_COL_TX), 0);
        lv_label_set_long_mode(s_io.lbl_spi_cmp, LV_LABEL_LONG_CLIP);
        refresh_spi_cmp();
        make_chip(comp, "字", 28, on_spi_byte, false);
        make_chip(comp, "长", 28, on_spi_len, false);
        make_chip(comp, "发", 32, on_spi, true);
        make_chip(comp, "JEDEC", 52, on_spi_jedec, false);
        make_chip(comp, "READ", 44, on_spi_flash_rd, true);
    }

    ui_frame_list_init(list, tab);
    lv_obj_set_flex_grow(list->cont, 1);
    lv_obj_set_style_pad_all(list->cont, 3, 0);
}

lv_obj_t *ui_page_io_create(lv_obj_t *parent)
{
    memset(&s_io, 0, sizeof(s_io));
    s_io.i2c_addr = 0x50;
    s_io.i2c_len = 1;
    s_io.i2c_data[0] = 0x00;
    s_io.spi_len = 4;
    s_io.spi_data[0] = 0x9F;

    s_io.root = lv_obj_create(parent);
    lv_obj_set_size(s_io.root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_io.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_io.root, 0, 0);
    lv_obj_set_style_pad_all(s_io.root, 4, 0);
    lv_obj_set_flex_flow(s_io.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_io.root, 4, 0);
    lv_obj_remove_flag(s_io.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = ui_make_toolbar(s_io.root, 24);
    s_io.kpi = lv_label_create(top);
    lv_obj_set_style_text_font(s_io.kpi, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_io.kpi, ui_color(UI_COL_ACCENT), 0);
    lv_label_set_text(s_io.kpi, "I2C / SPI / PWM / 采样");
    lv_obj_set_flex_grow(s_io.kpi, 1);

    s_io.tv = lv_tabview_create(s_io.root);
    lv_obj_set_flex_grow(s_io.tv, 1);
    lv_obj_set_width(s_io.tv, lv_pct(100));

    lv_obj_t *ti = lv_tabview_add_tab(s_io.tv, "I2C");
    lv_obj_t *ts = lv_tabview_add_tab(s_io.tv, "SPI");
    lv_obj_t *tp = lv_tabview_add_tab(s_io.tv, "PWM");
    lv_obj_t *ta = lv_tabview_add_tab(s_io.tv, "ADC");
    lv_obj_t *td = lv_tabview_add_tab(s_io.tv, "DIO");
    lv_obj_t *tw = lv_tabview_add_tab(s_io.tv, "1W");
    ui_style_tabview(s_io.tv, 32);

    build_bus_tab(ti, "扫描", true, on_scan, on_hold_i2c, &s_io.list_i2c,
                  &s_io.scope_i2c, BUS_SRC_I2C, false);
    build_bus_tab(ts, "发SPI", false, on_spi, on_hold_spi, &s_io.list_spi,
                  &s_io.scope_spi, BUS_SRC_SPI, true);
    paint_io_rates();

    /* PWM */
    lv_obj_set_style_pad_all(tp, 6, 0);
    lv_obj_set_flex_flow(tp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tp, 8, 0);
    lv_obj_set_style_bg_opa(tp, LV_OPA_TRANSP, 0);

    s_io.lbl_pwm = lv_label_create(tp);
    lv_obj_set_style_text_font(s_io.lbl_pwm, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_io.lbl_pwm, ui_color(UI_COL_TEXT), 0);

    lv_obj_t *row = ui_make_toolbar(tp, 36);
    lv_obj_t *pl = lv_label_create(row);
    lv_label_set_text(pl, "输出");
    lv_obj_set_style_text_font(pl, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(pl, ui_color(UI_COL_TEXT), 0);
    s_io.sw_pwm = lv_switch_create(row);
    lv_obj_add_event_cb(s_io.sw_pwm, on_pwm_sw, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *btns = lv_obj_create(tp);
    lv_obj_set_size(btns, lv_pct(100), 44);
    lv_obj_set_style_bg_opa(btns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btns, 0, 0);
    lv_obj_set_style_pad_all(btns, 0, 0);
    lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btns, 8, 0);

    s_io.btn_freq = lv_button_create(btns);
    ui_style_chip_btn(s_io.btn_freq, true);
    lv_obj_set_flex_grow(s_io.btn_freq, 1);
    lv_obj_set_height(s_io.btn_freq, 40);
    s_io.lbl_freq = lv_label_create(s_io.btn_freq);
    lv_obj_set_style_text_font(s_io.lbl_freq, UI_FONT_NUM14, 0);
    ui_btn_label_layout(s_io.btn_freq, s_io.lbl_freq);
    lv_obj_add_event_cb(s_io.btn_freq, on_freq, LV_EVENT_CLICKED, NULL);

    s_io.btn_duty = lv_button_create(btns);
    ui_style_chip_btn(s_io.btn_duty, false);
    lv_obj_set_flex_grow(s_io.btn_duty, 1);
    lv_obj_set_height(s_io.btn_duty, 40);
    s_io.lbl_duty = lv_label_create(s_io.btn_duty);
    lv_obj_set_style_text_font(s_io.lbl_duty, UI_FONT_NUM14, 0);
    ui_btn_label_layout(s_io.btn_duty, s_io.lbl_duty);
    lv_obj_add_event_cb(s_io.btn_duty, on_duty, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hint = lv_label_create(tp);
    lv_label_set_text(hint, "点按修改频率 / 占空比\nPWM 输出：GPIO11");
    lv_obj_set_style_text_font(hint, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(hint, ui_color(UI_COL_TEXT_DIM), 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));

    /* ADC */
    lv_obj_set_style_pad_all(ta, 6, 0);
    lv_obj_set_flex_flow(ta, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ta, 6, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, 0);

    s_io.lbl_adc = lv_label_create(ta);
    lv_obj_set_style_text_font(s_io.lbl_adc, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_io.lbl_adc, ui_color(UI_COL_ACCENT), 0);
    lv_label_set_text(s_io.lbl_adc, "采样 GPIO20");

    s_io.bar_adc = lv_bar_create(ta);
    lv_obj_set_size(s_io.bar_adc, lv_pct(100), 16);
    lv_bar_set_range(s_io.bar_adc, 0, 3300);
    lv_obj_set_style_bg_color(s_io.bar_adc, ui_color(UI_COL_PANEL3), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_io.bar_adc, ui_color(UI_COL_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_io.bar_adc, 3, 0);

    s_io.chart_adc = lv_chart_create(ta);
    lv_obj_set_width(s_io.chart_adc, lv_pct(100));
    lv_obj_set_flex_grow(s_io.chart_adc, 1);
    ui_style_scope_face(s_io.chart_adc);
    lv_obj_set_style_pad_all(s_io.chart_adc, 2, 0);
    lv_chart_set_type(s_io.chart_adc, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_io.chart_adc, BUS_ADC_HIST_LEN);
    lv_chart_set_range(s_io.chart_adc, LV_CHART_AXIS_PRIMARY_Y, 0, 3300);
    lv_chart_set_div_line_count(s_io.chart_adc, 4, 5);
    lv_obj_set_style_line_color(s_io.chart_adc, ui_color(UI_COL_SCOPE_GRID), LV_PART_MAIN);
    lv_obj_set_style_line_opa(s_io.chart_adc, LV_OPA_40, LV_PART_MAIN);
    ui_style_scope_trace(s_io.chart_adc);
    s_io.ser_adc = lv_chart_add_series(s_io.chart_adc, ui_color(UI_COL_GREEN), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *ah = lv_label_create(ta);
    lv_label_set_text(ah, "ADC1_CH4  0~3.3V");
    lv_obj_set_style_text_font(ah, UI_FONT_NUM14, 0);
    lv_obj_set_style_text_color(ah, ui_color(UI_COL_TEXT_DIM), 0);

    /* DIO */
    lv_obj_set_style_pad_all(td, 6, 0);
    lv_obj_set_flex_flow(td, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(td, 8, 0);
    s_io.lbl_dio = lv_label_create(td);
    lv_obj_set_style_text_font(s_io.lbl_dio, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_io.lbl_dio, ui_color(UI_COL_ACCENT), 0);
    dio_refresh();
    lv_obj_t *drow = lv_obj_create(td);
    lv_obj_set_size(drow, lv_pct(100), 28);
    lv_obj_set_style_bg_opa(drow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(drow, 0, 0);
    lv_obj_set_style_pad_all(drow, 0, 0);
    lv_obj_set_flex_flow(drow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(drow, 4, 0);
    make_chip(drow, "MODE", 48, on_dio_mode, false);
    make_chip(drow, "读", 36, on_dio_read, false);
    make_chip(drow, "1", 32, on_dio_hi, true);
    make_chip(drow, "0", 32, on_dio_lo, true);
    lv_obj_t *dh = lv_label_create(td);
    lv_label_set_text(dh, "探针默认 GPIO35（以太网拆除空闲脚）");
    lv_obj_set_style_text_font(dh, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(dh, ui_color(UI_COL_TEXT_DIM), 0);
    lv_label_set_long_mode(dh, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(dh, lv_pct(100));

    /* 1-Wire */
    lv_obj_set_style_pad_all(tw, 6, 0);
    lv_obj_set_flex_flow(tw, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tw, 8, 0);
    s_io.lbl_ow = lv_label_create(tw);
    lv_obj_set_style_text_font(s_io.lbl_ow, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_io.lbl_ow, ui_color(UI_COL_ACCENT), 0);
    lv_label_set_text(s_io.lbl_ow, "1-Wire 与 DIO 共用脚");
    lv_label_set_long_mode(s_io.lbl_ow, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_io.lbl_ow, lv_pct(100));
    lv_obj_t *wrow = lv_obj_create(tw);
    lv_obj_set_size(wrow, lv_pct(100), 28);
    lv_obj_set_style_bg_opa(wrow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrow, 0, 0);
    lv_obj_set_style_pad_all(wrow, 0, 0);
    lv_obj_set_flex_flow(wrow, LV_FLEX_FLOW_ROW);
    make_chip(wrow, "SCAN", 56, on_ow_scan, true);
    lv_obj_t *wh = lv_label_create(tw);
    lv_label_set_text(wh, "接 DQ→GPIO35，外加 4.7k 上拉到 3.3V");
    lv_obj_set_style_text_font(wh, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(wh, ui_color(UI_COL_TEXT_DIM), 0);
    lv_label_set_long_mode(wh, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wh, lv_pct(100));

    (void)siggen_pwm_init();
    (void)bus_adc_init();
    (void)bus_dio_init(BUS_DIO_GPIO_DEFAULT);
    (void)bus_ow_init(bus_dio_get_gpio());
    paint_pwm_btns();

    return s_io.root;
}

void ui_page_io_update(void)
{
    if (!s_io.kpi || !s_io.tv) {
        return;
    }
    static uint8_t s_div;
    s_div++;
    bus_app_status_t st;
    bus_app_get_status(&st);
    char buf[72];
    snprintf(buf, sizeof(buf), "I2C %.0f帧/秒  SPI %.0f帧/秒",
             (double)st.ch[BUS_SRC_I2C].fps, (double)st.ch[BUS_SRC_SPI].fps);
    ui_label_set_if_changed(s_io.kpi, buf);

    uint32_t tab = lv_tabview_get_tab_active(s_io.tv);
    bus_frame_t frames[BUS_LIST_ROWS];

    if (tab == IO_TAB_I2C) {
        ui_scope_update_load(&s_io.scope_i2c);
        if (!s_io.hold_i2c && (s_div & 1u) == 0u) {
            size_t n = bus_capture_snapshot_src(BUS_SRC_I2C, frames, BUS_LIST_ROWS);
            ui_frame_list_update(&s_io.list_i2c, frames, n, true);
        }
    } else if (tab == IO_TAB_SPI) {
        ui_scope_update_load(&s_io.scope_spi);
        if (!s_io.hold_spi && (s_div & 1u) == 0u) {
            size_t n = bus_capture_snapshot_src(BUS_SRC_SPI, frames, BUS_LIST_ROWS);
            ui_frame_list_update(&s_io.list_spi, frames, n, true);
        }
    } else if (tab == IO_TAB_PWM) {
        if (!ui_numpad_is_open()) {
            paint_pwm_btns();
            ui_switch_set_checked(s_io.sw_pwm, siggen_pwm_is_running());
        }
    } else if (tab == IO_TAB_ADC && s_io.lbl_adc) {
        bus_adc_sample_t smp;
        if (bus_adc_read_avg(&smp, 8) == ESP_OK) {
            bus_adc_hist_push(smp.mv);
            snprintf(buf, sizeof(buf), "引脚%d  %d 毫伏  原始 %d%s",
                     (int)smp.gpio, smp.mv, smp.raw, smp.calibrated ? "" : " ~");
            ui_label_set_if_changed(s_io.lbl_adc, buf);
            lv_bar_set_value(s_io.bar_adc, smp.mv, LV_ANIM_OFF);

            uint16_t hist[BUS_ADC_HIST_LEN];
            size_t hn = bus_adc_get_hist(hist, BUS_ADC_HIST_LEN);
            for (uint32_t i = 0; i < BUS_ADC_HIST_LEN; i++) {
                int32_t v = (i < hn) ? (int32_t)hist[i] : 0;
                lv_chart_set_series_value_by_id(s_io.chart_adc, s_io.ser_adc, i, v);
            }
            lv_chart_refresh(s_io.chart_adc);
        }
    } else if (tab == IO_TAB_DIO) {
        if ((s_div & 3u) == 0u) {
            dio_refresh();
        }
    }
}
