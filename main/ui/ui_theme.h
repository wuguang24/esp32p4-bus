/**
 * @file ui_theme.h
 * @brief 示波器深色工业：CRT 近黑 + 磷光青绿 + 冷钢描边
 */

#pragma once

#include "lvgl.h"
#include "fonts/lv_font_bus_ui_16.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/** 勿在 LVGL 定时器/事件回调内调用（持锁时 vTaskDelay 会破坏对象树） */
static inline void ui_yield(void)
{
    /* intentionally empty */
}

/** 仅文本变化时 set_text，减少 FULL 刷新模式下整屏重绘 */
static inline void ui_label_set_if_changed(lv_obj_t *lbl, const char *txt)
{
    if (lbl == NULL || txt == NULL) {
        return;
    }
    const char *cur = lv_label_get_text(lbl);
    if (cur == NULL || strcmp(cur, txt) != 0) {
        lv_label_set_text(lbl, txt);
    }
}
#define UI_COL_BG           0x05070A   /* CRT 近黑 */
#define UI_COL_BG_DEEP      0x020304
#define UI_COL_PANEL        0x0C1218
#define UI_COL_PANEL2       0x121A22
#define UI_COL_PANEL3       0x18222C
#define UI_COL_BORDER       0x243040
#define UI_COL_BORDER_LIT   0x3A5060
#define UI_COL_ACCENT       0x2EE8C8   /* 磷光青绿 */
#define UI_COL_ACCENT_DIM   0x0E6B5C
#define UI_COL_ACCENT_GLOW  0x083D36
#define UI_COL_BLUE         0x4AA3FF   /* 示波器蓝迹 */
#define UI_COL_AMBER        0xFFB84A   /* 独立琥珀，勿再别名蓝 */
#define UI_COL_RED          0xFF4D57   /* 触发/错误 */
#define UI_COL_GREEN        0x3DFF8A
#define UI_COL_TEXT         0xD8E6F0
#define UI_COL_TEXT_DIM     0x7A8B9A
#define UI_COL_MUTED        0x4A5A68
#define UI_COL_STEEL        0x8AA0B0
#define UI_COL_WHITE        0xF0F6FA
#define UI_COL_CH_TARGET    UI_COL_ACCENT
#define UI_COL_CH_ANGLE     0x8EC8FF
#define UI_COL_CH_UQ        UI_COL_BLUE
#define UI_COL_TX           UI_COL_AMBER
#define UI_COL_RX           0xD8E6F0

/* 示波器通道色（绿 / 青 / 蓝） */
#define UI_COL_SCOPE_CH1    0x3DFF8A
#define UI_COL_SCOPE_CH2    0x2EE8C8
#define UI_COL_SCOPE_CH3    0x4AA3FF
#define UI_COL_SCOPE_GRID   0x1A2A34
#define UI_COL_SCOPE_FACE   0x030507
#define UI_COL_HDR_STRIP    0x1A2834

/* 五通道色带（总览行左侧） */
#define UI_COL_CH_CAN       UI_COL_SCOPE_CH1
#define UI_COL_CH_RS485     UI_COL_SCOPE_CH2
#define UI_COL_CH_UART      UI_COL_SCOPE_CH3
#define UI_COL_CH_I2C       UI_COL_AMBER
#define UI_COL_CH_SPI       UI_COL_STEEL

#define UI_FONT_CN          (&lv_font_bus_ui_16)
#define UI_FONT_NUM14       (&lv_font_montserrat_14)
#define UI_FONT_NUM20       (&lv_font_montserrat_20)
#define UI_FONT_NUM24       (&lv_font_montserrat_24)
#define UI_FONT_ICON        (&lv_font_montserrat_20)  /* FontAwesome 符号 */
#define UI_FONT_ICON_SM     (&lv_font_montserrat_14)

/*
 * 外壳尺寸（跟随 lcddev.dir）：
 * 横屏 480×320：左侧导航 + 顶栏 + 内容
 * 竖屏 320×480：顶栏 + 内容 + 底导航
 */
#define UI_HDR_H            30
#define UI_NAV_H            48
#define UI_NAV_SIDE_W       52
#define UI_PAD              4
#define UI_CHROME_BORDER    1
#define UI_CONTENT_H(disp_ver) \
    ((disp_ver) - UI_HDR_H - UI_NAV_H - UI_CHROME_BORDER)
/** 兼容旧引用 */
#define UI_NAV_W            UI_NAV_SIDE_W

static inline bool ui_is_landscape(void)
{
    return lv_display_get_horizontal_resolution(NULL) >
           lv_display_get_vertical_resolution(NULL);
}

/** 按钮内标签：清除 theme 默认 padding，flex 居中（避免中文偏移） */
static inline void ui_btn_label_layout(lv_obj_t *btn, lv_obj_t *label)
{
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
}

static inline lv_color_t ui_color(uint32_t hex)
{
    return lv_color_hex(hex);
}

/** 全屏底：纯色（FULL 刷新下渐变极耗 CPU，易触发 WDT） */
static inline void ui_style_screen_bg(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, ui_color(UI_COL_BG), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

/** 示波器面板：细钢边 + 深腔 */
static inline void ui_style_panel(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, ui_color(UI_COL_PANEL), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, ui_color(UI_COL_BORDER), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 4, 0);
    lv_obj_set_style_pad_all(obj, 6, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_FULL, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/** 带左侧色条的 KPI / 状态条 */
static inline void ui_style_panel_accent(lv_obj_t *obj, uint32_t accent)
{
    ui_style_panel(obj);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, ui_color(accent), 0);
    lv_obj_set_style_pad_left(obj, 8, 0);
}

/** 示波屏幕面（帧列表 / 波形腔） */
static inline void ui_style_scope_face(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, ui_color(UI_COL_SCOPE_FACE), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, ui_color(UI_COL_BORDER_LIT), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 3, 0);
    lv_obj_set_style_pad_all(obj, 4, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

/** 示波器细迹线：覆盖默认主题 3px 线 + 8px 圆点 */
static inline void ui_style_scope_trace(lv_obj_t *chart)
{
    if (!chart) {
        return;
    }
    lv_obj_set_style_line_width(chart, 1, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart, false, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_INDICATOR);
}

static inline void ui_style_chip_btn(lv_obj_t *btn, bool primary)
{
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_hor(btn, 4, 0);
    if (primary) {
        lv_obj_set_style_bg_color(btn, ui_color(UI_COL_ACCENT_DIM), 0);
        lv_obj_set_style_bg_color(btn, ui_color(UI_COL_ACCENT), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, ui_color(UI_COL_ACCENT), 0);
        lv_obj_set_style_text_color(btn, ui_color(UI_COL_WHITE), 0);
    } else {
        lv_obj_set_style_bg_color(btn, ui_color(UI_COL_PANEL2), 0);
        lv_obj_set_style_bg_color(btn, ui_color(UI_COL_PANEL3), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, ui_color(UI_COL_BORDER_LIT), 0);
        lv_obj_set_style_text_color(btn, ui_color(UI_COL_TEXT), 0);
    }
}

/** 工具条面板：略亮于底，与示波腔区分 */
static inline void ui_style_toolbar_panel(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, ui_color(UI_COL_PANEL2), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, ui_color(UI_COL_BORDER), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_hor(obj, 3, 0);
    lv_obj_set_style_pad_ver(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

/** 深色 TabView：适配 320 宽多 Tab */
static inline void ui_style_tabview(lv_obj_t *tv, int32_t bar_h)
{
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tv, 0, 0);
    lv_tabview_set_tab_bar_size(tv, bar_h);

    lv_obj_t *bar = lv_tabview_get_tab_bar(tv);
    if (bar) {
        lv_obj_set_style_bg_color(bar, ui_color(UI_COL_PANEL), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 2, 0);
        lv_obj_set_style_pad_column(bar, 2, 0);
        uint32_t n = lv_obj_get_child_count(bar);
        for (uint32_t i = 0; i < n; i++) {
            lv_obj_t *btn = lv_obj_get_child(bar, i);
            if (!btn) {
                continue;
            }
            lv_obj_set_style_bg_color(btn, ui_color(UI_COL_PANEL2), 0);
            lv_obj_set_style_bg_color(btn, ui_color(UI_COL_ACCENT_GLOW), LV_STATE_CHECKED);
            lv_obj_set_style_text_color(btn, ui_color(UI_COL_MUTED), 0);
            lv_obj_set_style_text_color(btn, ui_color(UI_COL_ACCENT), LV_STATE_CHECKED);
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_pad_hor(btn, 2, 0);
            /* 含「采样」等中文 Tab，必须用 CN 字库（Montserrat 会方框） */
            lv_obj_set_style_text_font(btn, UI_FONT_CN, 0);
        }
    }
    lv_obj_t *cont = lv_tabview_get_content(tv);
    if (cont) {
        lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(cont, 0, 0);
        lv_obj_set_style_border_width(cont, 0, 0);
    }
}

/** 透明工具行（KPI / 开关） */
static inline lv_obj_t *ui_make_toolbar(lv_obj_t *parent, int32_t h)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), h);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

/** 同步开关外观（程序化改 CHECKED，一般不走点击回调） */
static inline void ui_switch_set_checked(lv_obj_t *sw, bool on)
{
    if (!sw) {
        return;
    }
    if (lv_obj_has_state(sw, LV_STATE_CHECKED) == on) {
        return;
    }
    lv_obj_set_state(sw, LV_STATE_CHECKED, on);
}

/** 页标题：图标(Montserrat) + 中文 */
static inline void ui_make_section_title(lv_obj_t *parent, const char *symbol,
                                        const char *cn, lv_obj_t **out_row)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 30);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ic = lv_label_create(row);
    lv_label_set_text(ic, symbol);
    lv_obj_set_style_text_font(ic, UI_FONT_ICON, 0);
    lv_obj_set_style_text_color(ic, ui_color(UI_COL_ACCENT), 0);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *tx = lv_label_create(row);
    lv_label_set_text(tx, cn);
    lv_obj_set_style_text_font(tx, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(tx, ui_color(UI_COL_ACCENT), 0);
    lv_label_set_long_mode(tx, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(tx, lv_pct(100));
    lv_obj_align(tx, LV_ALIGN_LEFT_MID, 30, 0);

    if (out_row) {
        *out_row = row;
    }
}
