/**
 * @file ui_shell.c
 * @brief 总线分析仪外壳：横屏左侧导航 / 竖屏底栏
 */

#include "ui_pages.h"
#include "ui_theme.h"
#include "ui_numpad.h"
#include "bus_app.h"
#include "bus_capture.h"
#include "wifi_bringup.h"
#include "board_alarm_led.h"
#include "lv_mainstart.h"

#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_shell";

typedef enum {
    UI_PAGE_OVERVIEW = 0,
    UI_PAGE_BUS,
    UI_PAGE_UART,
    UI_PAGE_IO,
    UI_PAGE_SETUP,
    UI_PAGE_COUNT
} ui_page_id_t;

typedef struct {
    lv_obj_t *scr;
    lv_obj_t *hdr_title;
    lv_obj_t *hdr_state;
    lv_obj_t *content;
    lv_obj_t *pages[UI_PAGE_COUNT];
    lv_obj_t *nav_btns[UI_PAGE_COUNT];
    lv_obj_t *nav_icons[UI_PAGE_COUNT];
    lv_obj_t *nav_labels[UI_PAGE_COUNT];
    ui_page_id_t active;
    lv_timer_t *timer;
    uint8_t preload_idx;
    uint8_t tick;
    ui_page_id_t pending_nav;
    bool landscape;
} ui_shell_t;

static ui_shell_t s_ui;

static void ensure_page(ui_page_id_t id);
static void show_page(ui_page_id_t id);

static const char *s_nav_icon[UI_PAGE_COUNT] = {
    LV_SYMBOL_HOME, LV_SYMBOL_LIST, LV_SYMBOL_REFRESH, LV_SYMBOL_DIRECTORY, LV_SYMBOL_SETTINGS
};
static const char *s_nav_cn[UI_PAGE_COUNT] = {
    "总览", "总线", "串口", "扩展", "设置"
};
static const char *s_titles[UI_PAGE_COUNT] = {
    "BUS·SCOPE", "CAN / 485", "UART CH", "扩展 IO", "系统设置"
};

static const ui_page_id_t s_preload_pages[] = {
    UI_PAGE_OVERVIEW,
    UI_PAGE_BUS,
    UI_PAGE_UART,
    UI_PAGE_IO,
    UI_PAGE_SETUP,
};

static void apply_nav_style(ui_page_id_t id, bool on)
{
    if (id >= UI_PAGE_COUNT || s_ui.nav_btns[id] == NULL) {
        return;
    }
    static const uint32_t s_nav_accent[UI_PAGE_COUNT] = {
        UI_COL_ACCENT, UI_COL_SCOPE_CH1, UI_COL_SCOPE_CH3, UI_COL_AMBER, UI_COL_STEEL
    };
    uint32_t acc = s_nav_accent[id];
    if (on) {
        lv_obj_add_state(s_ui.nav_btns[id], LV_STATE_CHECKED);
        lv_obj_set_style_text_color(s_ui.nav_icons[id], ui_color(acc), 0);
        lv_obj_set_style_text_color(s_ui.nav_labels[id], ui_color(acc), 0);
        lv_obj_set_style_border_color(s_ui.nav_btns[id], ui_color(acc), LV_STATE_CHECKED);
        lv_obj_set_style_border_side(s_ui.nav_btns[id],
                                     s_ui.landscape ? LV_BORDER_SIDE_LEFT : LV_BORDER_SIDE_TOP,
                                     LV_STATE_CHECKED);
        lv_obj_set_style_border_width(s_ui.nav_btns[id], 2, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_ui.nav_btns[id], LV_STATE_CHECKED);
        lv_obj_set_style_text_color(s_ui.nav_icons[id], ui_color(UI_COL_MUTED), 0);
        lv_obj_set_style_text_color(s_ui.nav_labels[id], ui_color(UI_COL_MUTED), 0);
        lv_obj_set_style_border_width(s_ui.nav_btns[id], 1, 0);
        lv_obj_set_style_border_side(s_ui.nav_btns[id], LV_BORDER_SIDE_FULL, 0);
    }
}

static void anim_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void page_fade_in(lv_obj_t *page)
{
    if (page == NULL) {
        return;
    }
    lv_anim_delete(page, anim_opa_cb);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(page, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, page);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, 90);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_start(&a);
}

static void page_set_hidden(lv_obj_t *page, bool hide)
{
    if (page == NULL) {
        return;
    }
    if (hide) {
        lv_anim_delete(page, anim_opa_cb);
        lv_obj_set_style_opa(page, LV_OPA_COVER, 0);
        lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(page, LV_OPA_COVER, 0);
    }
}

static void show_page(ui_page_id_t id)
{
    if (id >= UI_PAGE_COUNT) {
        return;
    }
    if (ui_numpad_is_open()) {
        ui_numpad_close();
    }
    ensure_page(id);
    if (s_ui.pages[id] == NULL) {
        return;
    }

    ui_page_id_t prev = s_ui.active;
    /* 只隐上一页，避免整屏级重绘（对齐 FOC） */
    if (prev < UI_PAGE_COUNT && prev != id && s_ui.pages[prev] != NULL) {
        page_set_hidden(s_ui.pages[prev], true);
        apply_nav_style(prev, false);
    }
    if (prev < UI_PAGE_COUNT && prev != id) {
        page_fade_in(s_ui.pages[id]);
    } else {
        page_set_hidden(s_ui.pages[id], false);
    }
    apply_nav_style(id, true);
    s_ui.active = id;
    ui_label_set_if_changed(s_ui.hdr_title, s_titles[id]);

    if (s_ui.timer) {
        /* 略降 UI 刷新频率，减轻 FULL+PPA；hist 仍由 tick 推进 */
        uint32_t ms = (id == UI_PAGE_SETUP) ? 100 : 40;
        lv_timer_set_period(s_ui.timer, ms);
        lv_timer_ready(s_ui.timer);
    }
}

static void on_nav(lv_event_t *e)
{
    ui_page_id_t id = (ui_page_id_t)(uintptr_t)lv_event_get_user_data(e);
    if (id >= UI_PAGE_COUNT || id == s_ui.active) {
        return;
    }
    if (s_ui.pages[id] != NULL) {
        s_ui.pending_nav = UI_PAGE_COUNT;
        show_page(id);
    } else {
        if (s_ui.active < UI_PAGE_COUNT) {
            apply_nav_style(s_ui.active, false);
        }
        apply_nav_style(id, true);
        s_ui.pending_nav = id;
    }
}

static void ensure_page(ui_page_id_t id)
{
    if (id >= UI_PAGE_COUNT || s_ui.pages[id] || s_ui.content == NULL) {
        return;
    }
    switch (id) {
    case UI_PAGE_OVERVIEW:
        s_ui.pages[id] = ui_page_overview_create(s_ui.content);
        break;
    case UI_PAGE_BUS:
        s_ui.pages[id] = ui_page_bus_create(s_ui.content);
        break;
    case UI_PAGE_UART:
        s_ui.pages[id] = ui_page_uart_create(s_ui.content);
        break;
    case UI_PAGE_IO:
        s_ui.pages[id] = ui_page_io_create(s_ui.content);
        break;
    case UI_PAGE_SETUP:
        s_ui.pages[id] = ui_page_setup_create(s_ui.content);
        break;
    default:
        break;
    }
    if (s_ui.pages[id]) {
        lv_obj_set_size(s_ui.pages[id], lv_pct(100), lv_pct(100));
        lv_obj_align(s_ui.pages[id], LV_ALIGN_TOP_LEFT, 0, 0);
        page_set_hidden(s_ui.pages[id], true);
    }
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    /* 切页预创建时仍推进 hist，避免波形停住 */
    s_ui.tick++;
    bus_capture_tick();
    board_alarm_led_poll();

    if (s_ui.pending_nav < UI_PAGE_COUNT) {
        return;
    }
    if (ui_numpad_is_open()) {
        return;
    }

    /* 顶栏约每 80ms */
    if ((s_ui.tick % 2u) == 0u) {
        bus_app_status_t st;
        bus_app_get_status(&st);
        char stbuf[48];
        if (st.recording) {
            snprintf(stbuf, sizeof(stbuf), "REC %u/%u D%lu",
                     (unsigned)st.stats.ring_used, (unsigned)st.stats.ring_cap,
                     (unsigned long)st.stats.drop_count);
        } else {
            snprintf(stbuf, sizeof(stbuf), "%u/%u D%lu %.0ffps",
                     (unsigned)st.stats.ring_used, (unsigned)st.stats.ring_cap,
                     (unsigned long)st.stats.drop_count, (double)st.stats.rx_rate);
        }
        ui_label_set_if_changed(s_ui.hdr_state, stbuf);
    }

    if (s_ui.pages[s_ui.active] == NULL) {
        return;
    }
    switch (s_ui.active) {
    case UI_PAGE_OVERVIEW: ui_page_overview_update(); break;
    case UI_PAGE_BUS:      ui_page_bus_update(); break;
    case UI_PAGE_UART:     ui_page_uart_update(); break;
    case UI_PAGE_IO:       ui_page_io_update(); break;
    case UI_PAGE_SETUP:
        if ((s_ui.tick % 3u) == 0u) {
            ui_page_setup_update();
        }
        break;
    default: break;
    }
}

static lv_obj_t *make_nav_btn(lv_obj_t *nav, int i, bool side)
{
    lv_obj_t *btn = lv_button_create(nav);
    s_ui.nav_btns[i] = btn;
    if (side) {
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_flex_grow(btn, 1);
    } else {
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, UI_NAV_H - 4);
    }
    lv_obj_set_style_bg_color(btn, ui_color(UI_COL_PANEL), 0);
    lv_obj_set_style_bg_color(btn, ui_color(UI_COL_HDR_STRIP), LV_STATE_CHECKED);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, ui_color(UI_COL_BORDER), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_pad_row(btn, 0, 0);

    s_ui.nav_icons[i] = lv_label_create(btn);
    lv_label_set_text(s_ui.nav_icons[i], s_nav_icon[i]);
    lv_obj_set_style_text_font(s_ui.nav_icons[i], UI_FONT_ICON_SM, 0);

    s_ui.nav_labels[i] = lv_label_create(btn);
    lv_label_set_text(s_ui.nav_labels[i], s_nav_cn[i]);
    lv_obj_set_style_text_font(s_ui.nav_labels[i], UI_FONT_CN, 0);

    lv_obj_add_event_cb(btn, on_nav, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    return btn;
}

void ui_shell_create(void)
{
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.pending_nav = UI_PAGE_COUNT;
    s_ui.landscape = ui_is_landscape();
    board_alarm_led_init();

    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        lv_display_enable_invalidation(disp, false);
    }

    s_ui.scr = lv_obj_create(NULL);
    ui_style_screen_bg(s_ui.scr);
    lv_obj_remove_flag(s_ui.scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr;
    lv_obj_t *nav;

    if (s_ui.landscape) {
        lv_obj_t *row = lv_obj_create(s_ui.scr);
        lv_obj_set_size(row, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        nav = lv_obj_create(row);
        lv_obj_set_size(nav, UI_NAV_SIDE_W, lv_pct(100));
        lv_obj_set_style_bg_color(nav, ui_color(UI_COL_BG_DEEP), 0);
        lv_obj_set_style_border_width(nav, 1, 0);
        lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_RIGHT, 0);
        lv_obj_set_style_border_color(nav, ui_color(UI_COL_BORDER), 0);
        lv_obj_set_style_pad_all(nav, 2, 0);
        lv_obj_set_style_radius(nav, 0, 0);
        lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *col = lv_obj_create(row);
        lv_obj_set_height(col, lv_pct(100));
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        hdr = lv_obj_create(col);
        lv_obj_set_size(hdr, lv_pct(100), UI_HDR_H);

        s_ui.content = lv_obj_create(col);
        lv_obj_set_width(s_ui.content, lv_pct(100));
        lv_obj_set_flex_grow(s_ui.content, 1);
    } else {
        hdr = lv_obj_create(s_ui.scr);
        lv_obj_set_size(hdr, lv_pct(100), UI_HDR_H);
        lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);

        nav = lv_obj_create(s_ui.scr);
        lv_obj_set_size(nav, lv_pct(100), UI_NAV_H);
        lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(nav, ui_color(UI_COL_PANEL), 0);
        lv_obj_set_style_border_width(nav, 0, 0);
        lv_obj_set_style_pad_all(nav, 2, 0);
        lv_obj_set_style_radius(nav, 0, 0);
        lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

        s_ui.content = lv_obj_create(s_ui.scr);
        lv_obj_set_width(s_ui.content, lv_pct(100));
        lv_obj_align(s_ui.content, LV_ALIGN_TOP_MID, 0, UI_HDR_H + UI_CHROME_BORDER);
        lv_obj_set_height(s_ui.content,
                          lv_display_get_vertical_resolution(NULL) - UI_HDR_H - UI_NAV_H - UI_CHROME_BORDER);
    }

    lv_obj_set_style_bg_color(hdr, ui_color(UI_COL_BG_DEEP), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, ui_color(UI_COL_ACCENT_DIM), 0);
    lv_obj_set_style_pad_hor(hdr, 8, 0);
    lv_obj_set_style_pad_ver(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(hdr, 0, 0);

    /* 左侧触发色条 */
    lv_obj_t *trig = lv_obj_create(hdr);
    lv_obj_set_size(trig, 3, UI_HDR_H - 8);
    lv_obj_set_style_bg_color(trig, ui_color(UI_COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(trig, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(trig, 0, 0);
    lv_obj_set_style_radius(trig, 1, 0);
    lv_obj_align(trig, LV_ALIGN_LEFT_MID, 0, 0);

    s_ui.hdr_title = lv_label_create(hdr);
    lv_label_set_text(s_ui.hdr_title, s_titles[0]);
    lv_obj_set_style_text_font(s_ui.hdr_title, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_ui.hdr_title, ui_color(UI_COL_ACCENT), 0);
    lv_obj_align(s_ui.hdr_title, LV_ALIGN_LEFT_MID, 10, 0);

    s_ui.hdr_state = lv_label_create(hdr);
    lv_label_set_text(s_ui.hdr_state, "…");
    lv_obj_set_style_text_font(s_ui.hdr_state, UI_FONT_NUM14, 0);
    lv_obj_set_style_text_color(s_ui.hdr_state, ui_color(UI_COL_SCOPE_CH1), 0);
    lv_obj_align(s_ui.hdr_state, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_set_style_bg_color(s_ui.content, ui_color(UI_COL_BG), 0);
    lv_obj_set_style_bg_opa(s_ui.content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.content, 0, 0);
    lv_obj_set_style_pad_all(s_ui.content, 0, 0);
    lv_obj_set_style_radius(s_ui.content, 0, 0);
    lv_obj_remove_flag(s_ui.content, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        make_nav_btn(nav, i, s_ui.landscape);
    }

    if (!s_ui.landscape) {
        lv_obj_move_foreground(hdr);
        lv_obj_move_foreground(nav);
    }

    s_ui.active = UI_PAGE_OVERVIEW;
    s_ui.preload_idx = 0;
    s_ui.tick = 0;
    s_ui.timer = lv_timer_create(timer_cb, 40, NULL);

    if (disp) {
        lv_display_enable_invalidation(disp, true);
        lv_obj_invalidate(s_ui.scr);
    }

    lv_splash_finish();
    lv_splash_clear();
    lv_screen_load_anim(s_ui.scr, LV_SCREEN_LOAD_ANIM_FADE_IN, 220, 0, true);
    ESP_LOGI(TAG, "chrome ready %s pages via pump",
             s_ui.landscape ? "landscape" : "portrait");
}

bool ui_shell_pump(void)
{
    if (s_ui.pending_nav < UI_PAGE_COUNT) {
        ui_page_id_t id = s_ui.pending_nav;
        ensure_page(id);
        s_ui.pending_nav = UI_PAGE_COUNT;
        if (s_ui.pages[id]) {
            show_page(id);
        }
        return true;
    }

    const size_t n = sizeof(s_preload_pages) / sizeof(s_preload_pages[0]);
    if (s_ui.preload_idx >= n) {
        return false;
    }

    ui_page_id_t id = s_preload_pages[s_ui.preload_idx++];
    ensure_page(id);
    if (id == UI_PAGE_OVERVIEW && s_ui.pages[id]) {
        show_page(UI_PAGE_OVERVIEW);
    }
    if (s_ui.preload_idx >= n) {
        ESP_LOGI(TAG, "pages preload done");
    }
    return s_ui.preload_idx < n;
}
