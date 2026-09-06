/**
 * @file ui_page_overview.c
 * @brief 总览：五通道负载 + 示波腔活动波形
 */

#include "ui_pages.h"
#include "ui_theme.h"
#include "ui_scope.h"
#include "bus_app.h"
#include "bus_capture.h"
#include "wifi_bringup.h"
#include "esp_err.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t *root;
    lv_obj_t *lbl_ch[BUS_SRC_COUNT];
    lv_obj_t *bar[BUS_SRC_COUNT];
    lv_obj_t *lbl_wifi;
    lv_obj_t *lbl_rec;
    lv_obj_t *sw_rec;
    ui_scope_t scope;
    int16_t bar_val[BUS_SRC_COUNT];
    uint32_t bar_col[BUS_SRC_COUNT];
} overview_t;

static overview_t s_ov;
static const char *s_ch_name[BUS_SRC_COUNT] = {"CAN", "485", "串口", "I2C", "SPI"};
static const uint32_t s_ch_col[BUS_SRC_COUNT] = {
    UI_COL_CH_CAN, UI_COL_CH_RS485, UI_COL_CH_UART, UI_COL_CH_I2C, UI_COL_CH_SPI
};
/* 总览波形只画主三路，减轻 FULL+PPA 每帧绘制 */
static const uint8_t s_scope_srcs[] = {
    BUS_SRC_CAN, BUS_SRC_RS485, BUS_SRC_UART
};

static void on_rec(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        esp_err_t err = bus_app_logger_start();
        if (err != ESP_OK) {
            ui_switch_set_checked(sw, false);
        }
    } else {
        bus_app_logger_stop();
    }
}

static void on_clear(lv_event_t *e)
{
    (void)e;
    bus_app_clear_capture();
}

static void paint_bar(int idx, float load)
{
    lv_obj_t *bar = s_ov.bar[idx];
    if (!bar) {
        return;
    }
    int32_t v = (int32_t)(load + 0.5f);
    uint32_t col = UI_COL_GREEN;
    if (load >= 50.0f) {
        col = UI_COL_RED;
    } else if (load >= 10.0f) {
        col = s_ch_col[idx];
    }
    if (s_ov.bar_val[idx] != (int16_t)v) {
        s_ov.bar_val[idx] = (int16_t)v;
        lv_bar_set_value(bar, v, LV_ANIM_OFF);
    }
    if (s_ov.bar_col[idx] != col) {
        s_ov.bar_col[idx] = col;
        lv_obj_set_style_bg_color(bar, ui_color(col), LV_PART_INDICATOR);
    }
}

lv_obj_t *ui_page_overview_create(lv_obj_t *parent)
{
    memset(&s_ov, 0, sizeof(s_ov));
    for (int i = 0; i < BUS_SRC_COUNT; i++) {
        s_ov.bar_val[i] = -1;
    }

    s_ov.root = lv_obj_create(parent);
    lv_obj_set_size(s_ov.root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_ov.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ov.root, 0, 0);
    lv_obj_set_style_pad_all(s_ov.root, UI_PAD, 0);
    lv_obj_set_flex_flow(s_ov.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ov.root, 3, 0);
    lv_obj_add_flag(s_ov.root, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < BUS_SRC_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(s_ov.root);
        lv_obj_set_size(row, lv_pct(100), 22);
        lv_obj_set_style_bg_color(row, ui_color(UI_COL_PANEL), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(row, ui_color(s_ch_col[i]), 0);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_pad_hor(row, 6, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        s_ov.lbl_ch[i] = lv_label_create(row);
        lv_obj_set_width(s_ov.lbl_ch[i], ui_is_landscape() ? 148 : 120);
        lv_label_set_long_mode(s_ov.lbl_ch[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(s_ov.lbl_ch[i], UI_FONT_CN, 0);
        lv_obj_set_style_text_color(s_ov.lbl_ch[i], ui_color(s_ch_col[i]), 0);
        lv_label_set_text(s_ov.lbl_ch[i], s_ch_name[i]);

        s_ov.bar[i] = lv_bar_create(row);
        lv_obj_set_flex_grow(s_ov.bar[i], 1);
        lv_obj_set_height(s_ov.bar[i], 10);
        lv_bar_set_range(s_ov.bar[i], 0, 100);
        lv_obj_set_style_bg_color(s_ov.bar[i], ui_color(UI_COL_PANEL3), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_ov.bar[i], ui_color(s_ch_col[i]), LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_ov.bar[i], 2, 0);
        s_ov.bar_col[i] = s_ch_col[i];
    }

    lv_obj_t *cap = lv_label_create(s_ov.root);
    lv_label_set_text(cap, "负载波形  绿CAN 青485 蓝串口");
    lv_obj_set_style_text_font(cap, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(cap, ui_color(UI_COL_TEXT_DIM), 0);

    ui_scope_init(&s_ov.scope, s_ov.root, ui_is_landscape() ? 88 : 100,
                  s_scope_srcs, (uint8_t)(sizeof(s_scope_srcs) / sizeof(s_scope_srcs[0])));

    s_ov.lbl_wifi = lv_label_create(s_ov.root);
    lv_obj_set_style_text_font(s_ov.lbl_wifi, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_ov.lbl_wifi, ui_color(UI_COL_TEXT_DIM), 0);
    lv_label_set_text(s_ov.lbl_wifi, "无线网络 …");

    lv_obj_t *row = lv_obj_create(s_ov.root);
    lv_obj_set_size(row, lv_pct(100), 32);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *rec_lbl = lv_label_create(row);
    lv_label_set_text(rec_lbl, "卡录");
    lv_obj_set_style_text_font(rec_lbl, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(rec_lbl, ui_color(UI_COL_TEXT), 0);
    s_ov.sw_rec = lv_switch_create(row);
    lv_obj_add_event_cb(s_ov.sw_rec, on_rec, LV_EVENT_VALUE_CHANGED, NULL);

    s_ov.lbl_rec = lv_label_create(s_ov.root);
    lv_obj_set_width(s_ov.lbl_rec, lv_pct(100));
    lv_label_set_long_mode(s_ov.lbl_rec, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_ov.lbl_rec, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_ov.lbl_rec, ui_color(UI_COL_STEEL), 0);
    lv_label_set_text(s_ov.lbl_rec, "SD —");

    lv_obj_t *btn = lv_button_create(s_ov.root);
    ui_style_chip_btn(btn, false);
    lv_obj_set_size(btn, lv_pct(100), 30);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "清空缓冲");
    lv_obj_set_style_text_font(bl, UI_FONT_CN, 0);
    ui_btn_label_layout(btn, bl);
    lv_obj_add_event_cb(btn, on_clear, LV_EVENT_CLICKED, NULL);

    return s_ov.root;
}

void ui_page_overview_update(void)
{
    if (!s_ov.lbl_ch[0]) {
        return;
    }
    bus_app_status_t st;
    bus_app_get_status(&st);

    for (int i = 0; i < BUS_SRC_COUNT; i++) {
        char buf[48];
        const char *busy = (st.ch[i].busy_level == 2) ? "繁忙" :
                           (st.ch[i].busy_level == 1) ? "中等" : "空闲";
        snprintf(buf, sizeof(buf), "%s %s %.0f%%", s_ch_name[i], busy,
                 (double)st.ch[i].load_pct);
        ui_label_set_if_changed(s_ov.lbl_ch[i], buf);
        paint_bar(i, st.ch[i].load_pct);
    }

    ui_scope_update_load(&s_ov.scope);

    char buf[80];
    wifi_status_t ws;
    wifi_bringup_get_status(&ws);
    if (ws.ap_got_ip) {
        snprintf(buf, sizeof(buf), "热点 %s  %d.%d.%d.%d",
                 ws.ap_ssid[0] ? ws.ap_ssid : "?",
                 (int)((ws.ap_ip.addr) & 0xFF), (int)((ws.ap_ip.addr >> 8) & 0xFF),
                 (int)((ws.ap_ip.addr >> 16) & 0xFF), (int)((ws.ap_ip.addr >> 24) & 0xFF));
    } else if (ws.sta_got_ip) {
        snprintf(buf, sizeof(buf), "本机 %d.%d.%d.%d",
                 (int)((ws.sta_ip.addr) & 0xFF), (int)((ws.sta_ip.addr >> 8) & 0xFF),
                 (int)((ws.sta_ip.addr >> 16) & 0xFF), (int)((ws.sta_ip.addr >> 24) & 0xFF));
    } else {
        snprintf(buf, sizeof(buf), "无线 %s 等待…",
                 ws.ap_ssid[0] ? ws.ap_ssid : "…");
    }
    ui_label_set_if_changed(s_ov.lbl_wifi, buf);

    if (s_ov.lbl_rec) {
        char rbuf[72];
        if (st.recording) {
            char name[24];
            const char *path = st.logger.path;
            const char *base = path;
            if (path[0]) {
                const char *slash = strrchr(path, '/');
                if (slash && slash[1]) {
                    base = slash + 1;
                }
            } else {
                base = "";
            }
            strncpy(name, base, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            snprintf(rbuf, sizeof(rbuf), "录制中 %lu行 %s",
                     (unsigned long)st.logger.lines_written, name);
        } else if (st.logger.state == BUS_LOGGER_ERROR_NO_SD) {
            snprintf(rbuf, sizeof(rbuf), "SD未挂 插卡后设置页点SD");
        } else if (st.logger.state == BUS_LOGGER_ERROR_IO) {
            snprintf(rbuf, sizeof(rbuf), "录制IO失败 %s",
                     esp_err_to_name(st.logger.last_err));
        } else if (st.sd_mounted) {
            snprintf(rbuf, sizeof(rbuf), "SD已挂 可卡录");
        } else {
            snprintf(rbuf, sizeof(rbuf), "SD未挂");
        }
        ui_label_set_if_changed(s_ov.lbl_rec, rbuf);
    }

    ui_switch_set_checked(s_ov.sw_rec, st.recording);
}
