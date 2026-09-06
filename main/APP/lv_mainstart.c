/**
 ******************************************************************************
 * @file        lv_mainstart.c
 * @brief       开机闪屏：品牌揭示 → 进度收尾 → 交给 shell 淡入
 ******************************************************************************
 */

#include "lv_mainstart.h"
#include "ui_theme.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "lv_main";

static lv_obj_t *s_splash_bar;
static lv_obj_t *s_splash_hint;
static lv_obj_t *s_splash_brand;

static void splash_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void splash_bar_cb(void *obj, int32_t v)
{
    lv_bar_set_value((lv_obj_t *)obj, v, LV_ANIM_OFF);
}

static void splash_width_cb(void *obj, int32_t v)
{
    lv_obj_set_width((lv_obj_t *)obj, v);
}

static void splash_ty_cb(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)obj, v, 0);
}

static void splash_fade_in(lv_obj_t *obj, uint32_t delay_ms, uint32_t dur_ms)
{
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, dur_ms);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, splash_opa_cb);
    lv_anim_start(&a);
}

void lv_mainstart(void)
{
    s_splash_bar = NULL;
    s_splash_hint = NULL;
    s_splash_brand = NULL;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    ui_style_screen_bg(scr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *col = lv_obj_create(scr);
    lv_obj_set_size(col, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_center(col);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 12, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    s_splash_brand = lv_label_create(col);
    lv_label_set_text(s_splash_brand, "BUS");
    lv_obj_set_style_text_font(s_splash_brand, UI_FONT_NUM24, 0);
    lv_obj_set_style_text_color(s_splash_brand, ui_color(UI_COL_ACCENT), 0);
    lv_obj_set_style_text_letter_space(s_splash_brand, 8, 0);
    splash_fade_in(s_splash_brand, 30, 420);
    /* 轻微上浮，避免死板直出 */
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_splash_brand);
        lv_anim_set_values(&a, 12, 0);
        lv_anim_set_duration(&a, 480);
        lv_anim_set_delay(&a, 30);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, splash_ty_cb);
        lv_anim_start(&a);
    }

    lv_obj_t *rule = lv_obj_create(col);
    lv_obj_set_size(rule, 0, 2);
    lv_obj_set_style_radius(rule, 1, 0);
    lv_obj_set_style_bg_color(rule, ui_color(UI_COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_pad_all(rule, 0, 0);
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, rule);
        lv_anim_set_values(&a, 0, 64);
        lv_anim_set_duration(&a, 460);
        lv_anim_set_delay(&a, 160);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, splash_width_cb);
        lv_anim_start(&a);
    }

    lv_obj_t *sub = lv_label_create(col);
    lv_label_set_text(sub, "通信分析仪");
    lv_obj_set_style_text_font(sub, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(sub, ui_color(UI_COL_TEXT_DIM), 0);
    splash_fade_in(sub, 240, 380);

    s_splash_bar = lv_bar_create(col);
    lv_obj_set_size(s_splash_bar, 132, 3);
    lv_obj_set_style_radius(s_splash_bar, 2, 0);
    lv_obj_set_style_bg_color(s_splash_bar, ui_color(UI_COL_PANEL2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_splash_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_splash_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_splash_bar, ui_color(UI_COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_splash_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_splash_bar, 2, LV_PART_INDICATOR);
    lv_bar_set_range(s_splash_bar, 0, 100);
    lv_bar_set_value(s_splash_bar, 6, LV_ANIM_OFF);
    splash_fade_in(s_splash_bar, 360, 280);
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_splash_bar);
        lv_anim_set_values(&a, 6, 68);
        lv_anim_set_duration(&a, 1200);
        lv_anim_set_delay(&a, 400);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_reverse_duration(&a, 800);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&a, splash_bar_cb);
        lv_anim_start(&a);
    }

    s_splash_hint = lv_label_create(col);
    lv_label_set_text(s_splash_hint, "加载中…");
    lv_obj_set_style_text_font(s_splash_hint, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_splash_hint, ui_color(UI_COL_MUTED), 0);
    splash_fade_in(s_splash_hint, 400, 300);

    lv_refr_now(lv_display_get_default());
    ESP_LOGI(TAG, "splash 已刷新");
}

void lv_splash_finish(void)
{
    if (s_splash_bar != NULL && lv_obj_is_valid(s_splash_bar)) {
        lv_anim_del(s_splash_bar, splash_bar_cb);
        lv_bar_set_value(s_splash_bar, 100, LV_ANIM_OFF);
    }
    if (s_splash_hint != NULL && lv_obj_is_valid(s_splash_hint)) {
        lv_label_set_text(s_splash_hint, "就绪");
        lv_obj_set_style_text_color(s_splash_hint, ui_color(UI_COL_ACCENT), 0);
    }
    if (s_splash_brand != NULL && lv_obj_is_valid(s_splash_brand)) {
        lv_obj_set_style_text_color(s_splash_brand, ui_color(UI_COL_WHITE), 0);
    }
}

void lv_splash_clear(void)
{
    s_splash_bar = NULL;
    s_splash_hint = NULL;
    s_splash_brand = NULL;
}
