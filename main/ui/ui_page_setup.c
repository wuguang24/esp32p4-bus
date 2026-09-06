/**
 * @file ui_page_setup.c
 * @brief 五总线参数：波特率/校验/停止位/I2C·SPI 时钟与模式
 */

#include "ui_pages.h"
#include "ui_theme.h"
#include "ui_numpad.h"
#include "bus_app.h"
#include "bus_pins.h"
#include "wifi_bringup.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t *root;
    lv_obj_t *lbl_info;
    lv_obj_t *sw_tx;
    lv_obj_t *sw_listen;
    lv_obj_t *lbl_can;
    lv_obj_t *lbl_rs;
    lv_obj_t *lbl_rs_p;
    lv_obj_t *lbl_uart;
    lv_obj_t *lbl_uart_p;
    lv_obj_t *lbl_uart_s;
    lv_obj_t *lbl_i2c;
    lv_obj_t *lbl_spi;
    lv_obj_t *lbl_spi_m;
    lv_obj_t *lbl_wifi;
    lv_obj_t *lbl_sd;
} setup_page_t;

static setup_page_t s_sp;

static const uint32_t s_can_presets[] = {50000, 100000, 125000, 250000, 500000, 1000000};
static const uint32_t s_rs_presets[] = {9600, 19200, 38400, 57600, 115200, 230400};
static const uint32_t s_uart_presets[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
static const uint32_t s_i2c_presets[] = {100000, 400000};
static const uint32_t s_spi_presets[] = {100000, 1000000, 4000000, 10000000};

static const char *parity_rs_name(bus_rs485_parity_t p)
{
    switch (p) {
    case BUS_RS485_PARITY_EVEN: return "偶校验";
    case BUS_RS485_PARITY_ODD:  return "奇校验";
    default:                    return "无校验";
    }
}

static const char *parity_uart_name(bus_uart_parity_t p)
{
    switch (p) {
    case BUS_UART_PARITY_EVEN: return "偶校验";
    case BUS_UART_PARITY_ODD:  return "奇校验";
    default:                   return "无校验";
    }
}

static uint32_t next_preset(const uint32_t *tab, size_t n, uint32_t cur)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (tab[i] == cur) {
            return tab[(i + 1) % n];
        }
    }
    return tab[0];
}

static void paint_all(void)
{
    bus_app_status_t st;
    bus_app_get_status(&st);
    char buf[48];
    if (s_sp.lbl_can) {
        snprintf(buf, sizeof(buf), "CAN %lu bps", (unsigned long)st.can_baud);
        ui_label_set_if_changed(s_sp.lbl_can, buf);
    }
    if (s_sp.lbl_rs) {
        snprintf(buf, sizeof(buf), "485 %lu bps", (unsigned long)st.rs485_baud);
        ui_label_set_if_changed(s_sp.lbl_rs, buf);
    }
    if (s_sp.lbl_rs_p) {
        snprintf(buf, sizeof(buf), "485 %s", parity_rs_name(st.rs485_parity));
        ui_label_set_if_changed(s_sp.lbl_rs_p, buf);
    }
    if (s_sp.lbl_uart) {
        snprintf(buf, sizeof(buf), "串口 %lu", (unsigned long)st.uart_baud);
        ui_label_set_if_changed(s_sp.lbl_uart, buf);
    }
    if (s_sp.lbl_uart_p) {
        snprintf(buf, sizeof(buf), "串口 %s", parity_uart_name(st.uart_parity));
        ui_label_set_if_changed(s_sp.lbl_uart_p, buf);
    }
    if (s_sp.lbl_uart_s) {
        snprintf(buf, sizeof(buf), "停止位 %s",
                 st.uart_stop == BUS_UART_STOP_2 ? "2" : "1");
        ui_label_set_if_changed(s_sp.lbl_uart_s, buf);
    }
    if (s_sp.lbl_i2c) {
        snprintf(buf, sizeof(buf), "I2C %lu kHz", (unsigned long)(st.i2c_hz / 1000u));
        ui_label_set_if_changed(s_sp.lbl_i2c, buf);
    }
    if (s_sp.lbl_spi) {
        if (st.spi_hz >= 1000000u) {
            snprintf(buf, sizeof(buf), "SPI %.1f MHz", (double)st.spi_hz / 1e6);
        } else {
            snprintf(buf, sizeof(buf), "SPI %lu kHz", (unsigned long)(st.spi_hz / 1000u));
        }
        ui_label_set_if_changed(s_sp.lbl_spi, buf);
    }
    if (s_sp.lbl_spi_m) {
        snprintf(buf, sizeof(buf), "SPI Mode%u", (unsigned)st.spi_mode);
        ui_label_set_if_changed(s_sp.lbl_spi_m, buf);
    }
}

static void on_tx(lv_event_t *e)
{
    bus_app_set_allow_tx(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void on_listen(lv_event_t *e)
{
    bus_app_set_listen(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void on_can_ok(uint32_t v, void *ud)
{
    (void)ud;
    (void)bus_app_set_can_baud(v);
    paint_all();
}

static void on_rs_ok(uint32_t v, void *ud)
{
    (void)ud;
    (void)bus_app_set_rs485_baud(v);
    paint_all();
}

static void on_uart_ok(uint32_t v, void *ud)
{
    (void)ud;
    (void)bus_app_set_uart_baud(v);
    paint_all();
}

static void on_i2c_ok(uint32_t v, void *ud)
{
    (void)ud;
    (void)bus_app_set_i2c_hz(v);
    paint_all();
}

static void on_spi_ok(uint32_t v, void *ud)
{
    (void)ud;
    (void)bus_app_set_spi_hz(v);
    paint_all();
}

static void on_can_edit(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    ui_numpad_open("CAN 波特率", "40k~1M", st.can_baud,
                   BUS_CAN_BAUD_MIN, BUS_CAN_BAUD_MAX, on_can_ok, NULL);
}

static void on_can_cycle(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    (void)bus_app_set_can_baud(next_preset(s_can_presets,
        sizeof(s_can_presets) / sizeof(s_can_presets[0]), st.can_baud));
    paint_all();
}

static void on_rs_edit(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    ui_numpad_open("485 波特率", "≤250k", st.rs485_baud,
                   BUS_RS485_BAUD_MIN, BUS_RS485_BAUD_MAX, on_rs_ok, NULL);
}

static void on_rs_cycle(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    (void)bus_app_set_rs485_baud(next_preset(s_rs_presets,
        sizeof(s_rs_presets) / sizeof(s_rs_presets[0]), st.rs485_baud));
    paint_all();
}

static void on_rs_parity(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    bus_rs485_parity_t p = (bus_rs485_parity_t)((st.rs485_parity + 1) % 3);
    (void)bus_app_set_rs485_parity(p);
    paint_all();
}

static void on_uart_edit(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    ui_numpad_open("串口波特率", "1200~921600", st.uart_baud, 1200u, BUS_UART_BAUD_MAX,
                   on_uart_ok, NULL);
}

static void on_uart_cycle(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    (void)bus_app_set_uart_baud(next_preset(s_uart_presets,
        sizeof(s_uart_presets) / sizeof(s_uart_presets[0]), st.uart_baud));
    paint_all();
}

static void on_uart_parity(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    (void)bus_app_set_uart_parity((bus_uart_parity_t)((st.uart_parity + 1) % 3));
    paint_all();
}

static void on_uart_stop(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    (void)bus_app_set_uart_stop(st.uart_stop == BUS_UART_STOP_1 ? BUS_UART_STOP_2
                                                                : BUS_UART_STOP_1);
    paint_all();
}

static void on_i2c_edit(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    ui_numpad_open("I2C Hz", "10k~1M", st.i2c_hz, 10000u, 1000000u, on_i2c_ok, NULL);
}

static void on_i2c_cycle(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    (void)bus_app_set_i2c_hz(next_preset(s_i2c_presets, 2, st.i2c_hz));
    paint_all();
}

static void on_spi_edit(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    ui_numpad_open("SPI Hz", "10k~40M", st.spi_hz, 10000u, 40000000u, on_spi_ok, NULL);
}

static void on_spi_cycle(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    (void)bus_app_set_spi_hz(next_preset(s_spi_presets, 4, st.spi_hz));
    paint_all();
}

static void on_spi_mode(lv_event_t *e)
{
    (void)e;
    bus_app_status_t st;
    bus_app_get_status(&st);
    (void)bus_app_set_spi_mode((uint8_t)((st.spi_mode + 1) % 4));
    paint_all();
}

static void paint_wifi(void)
{
    if (!s_sp.lbl_wifi) {
        return;
    }
    wifi_status_t ws;
    wifi_bringup_get_status(&ws);
    char buf[56];
    snprintf(buf, sizeof(buf), "热点 %s",
             ws.ap_ssid[0] ? ws.ap_ssid : "ESP32P4-BUS");
    ui_label_set_if_changed(s_sp.lbl_wifi, buf);
}

static void on_wifi_reset(lv_event_t *e)
{
    (void)e;
    wifi_nvs_cfg_t cfg;
    if (!wifi_nvs_load(&cfg)) {
        memset(&cfg, 0, sizeof(cfg));
        snprintf(cfg.ap_ssid, sizeof(cfg.ap_ssid), "ESP32P4-BUS");
        snprintf(cfg.ap_pass, sizeof(cfg.ap_pass), "12345678");
        cfg.ap_channel = 6;
        cfg.ap_enabled = true;
        cfg.sta_enabled = true;
    }
    snprintf(cfg.ap_ssid, sizeof(cfg.ap_ssid), "ESP32P4-BUS");
    if (cfg.ap_pass[0] == '\0') {
        snprintf(cfg.ap_pass, sizeof(cfg.ap_pass), "12345678");
    }
    cfg.ap_enabled = true;
    (void)wifi_bringup_request_save_and_apply(&cfg);
    paint_wifi();
}

static void paint_sd(void)
{
    if (!s_sp.lbl_sd) {
        return;
    }
    bus_app_status_t st;
    bus_app_get_status(&st);
    ui_label_set_if_changed(s_sp.lbl_sd, st.sd_mounted ? "SD 已挂载" : "SD 未挂载");
}

static void on_sd_remount(lv_event_t *e)
{
    (void)e;
    (void)bus_app_sd_remount();
    paint_sd();
}

static lv_obj_t *make_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 34);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    return row;
}

static lv_obj_t *make_btn(lv_obj_t *row, lv_obj_t **out_lbl, int32_t grow,
                          lv_event_cb_t cb, bool primary)
{
    lv_obj_t *b = lv_button_create(row);
    ui_style_chip_btn(b, primary);
    lv_obj_set_height(b, 30);
    if (grow) {
        lv_obj_set_flex_grow(b, grow);
    } else {
        lv_obj_set_width(b, 52);
    }
    *out_lbl = lv_label_create(b);
    lv_obj_set_style_text_font(*out_lbl, UI_FONT_CN, 0);
    ui_btn_label_layout(b, *out_lbl);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static lv_obj_t *make_small(lv_obj_t *row, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(row);
    ui_style_chip_btn(b, false);
    lv_obj_set_size(b, 44, 30);
    lv_obj_t *lb = lv_label_create(b);
    lv_label_set_text(lb, txt);
    lv_obj_set_style_text_font(lb, UI_FONT_CN, 0);
    ui_btn_label_layout(b, lb);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

lv_obj_t *ui_page_setup_create(lv_obj_t *parent)
{
    memset(&s_sp, 0, sizeof(s_sp));
    s_sp.root = lv_obj_create(parent);
    lv_obj_set_size(s_sp.root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_sp.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_sp.root, 0, 0);
    lv_obj_set_style_pad_all(s_sp.root, UI_PAD, 0);
    lv_obj_set_flex_flow(s_sp.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_sp.root, 4, 0);
    lv_obj_add_flag(s_sp.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t;
    ui_make_section_title(s_sp.root, LV_SYMBOL_SETTINGS, "总线参数", &t);

    s_sp.lbl_info = lv_label_create(s_sp.root);
    lv_obj_set_style_text_font(s_sp.lbl_info, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_sp.lbl_info, ui_color(UI_COL_TEXT_DIM), 0);
    lv_label_set_long_mode(s_sp.lbl_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_sp.lbl_info, lv_pct(100));
    lv_label_set_text(s_sp.lbl_info, "点按改值 · 档=循环预设");

    lv_obj_t *row1 = ui_make_toolbar(s_sp.root, 32);
    lv_obj_t *l1 = lv_label_create(row1);
    lv_label_set_text(l1, "允许发送");
    lv_obj_set_style_text_font(l1, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(l1, ui_color(UI_COL_TEXT), 0);
    s_sp.sw_tx = lv_switch_create(row1);
    lv_obj_add_event_cb(s_sp.sw_tx, on_tx, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *row2 = ui_make_toolbar(s_sp.root, 32);
    lv_obj_t *l2 = lv_label_create(row2);
    lv_label_set_text(l2, "CAN 只听");
    lv_obj_set_style_text_font(l2, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(l2, ui_color(UI_COL_TEXT), 0);
    s_sp.sw_listen = lv_switch_create(row2);
    lv_obj_add_state(s_sp.sw_listen, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sp.sw_listen, on_listen, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *r;
    r = make_row(s_sp.root);
    make_btn(r, &s_sp.lbl_can, 1, on_can_edit, true);
    make_small(r, "档", on_can_cycle);

    r = make_row(s_sp.root);
    make_btn(r, &s_sp.lbl_rs, 1, on_rs_edit, false);
    make_small(r, "档", on_rs_cycle);
    make_btn(r, &s_sp.lbl_rs_p, 1, on_rs_parity, false);

    r = make_row(s_sp.root);
    make_btn(r, &s_sp.lbl_uart, 1, on_uart_edit, false);
    make_small(r, "档", on_uart_cycle);

    r = make_row(s_sp.root);
    make_btn(r, &s_sp.lbl_uart_p, 1, on_uart_parity, false);
    make_btn(r, &s_sp.lbl_uart_s, 1, on_uart_stop, false);

    r = make_row(s_sp.root);
    make_btn(r, &s_sp.lbl_i2c, 1, on_i2c_edit, false);
    make_small(r, "档", on_i2c_cycle);

    r = make_row(s_sp.root);
    make_btn(r, &s_sp.lbl_spi, 1, on_spi_edit, false);
    make_small(r, "档", on_spi_cycle);
    make_btn(r, &s_sp.lbl_spi_m, 1, on_spi_mode, false);

    r = make_row(s_sp.root);
    make_btn(r, &s_sp.lbl_wifi, 1, on_wifi_reset, false);
    make_small(r, "复位", on_wifi_reset);

    r = make_row(s_sp.root);
    make_btn(r, &s_sp.lbl_sd, 1, on_sd_remount, false);
    make_small(r, "重挂", on_sd_remount);

    paint_all();
    paint_wifi();
    paint_sd();
    return s_sp.root;
}

void ui_page_setup_update(void)
{
    if (!s_sp.lbl_info) {
        return;
    }
    if (ui_numpad_is_open()) {
        return;
    }
    bus_app_status_t st;
    bus_app_get_status(&st);
    char buf[168];
    snprintf(buf, sizeof(buf),
             "发送%s 只听%s  SD%s\nCAN%lu 485%lu UART%lu\nI2C%luk SPI%luk M%u",
             st.allow_tx ? "开" : "关",
             st.listen_only ? "开" : "关",
             st.sd_mounted ? "已挂" : "未挂",
             (unsigned long)st.can_baud, (unsigned long)st.rs485_baud,
             (unsigned long)st.uart_baud,
             (unsigned long)(st.i2c_hz / 1000u),
             (unsigned long)(st.spi_hz / 1000u),
             (unsigned)st.spi_mode);
    ui_label_set_if_changed(s_sp.lbl_info, buf);
    ui_switch_set_checked(s_sp.sw_tx, st.allow_tx);
    ui_switch_set_checked(s_sp.sw_listen, st.listen_only);
    paint_all();
    paint_wifi();
    paint_sd();
}
