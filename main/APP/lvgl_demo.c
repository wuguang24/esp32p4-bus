/**
 ******************************************************************************
 * @file        lvgl_demo.c
 * @brief       LVGL V9 — 逻辑横屏 480×320（PPA 硬件旋转 → 竖屏 DPI 320×480）
 *
 * 本板 MIPI+ST7796U 不能把 DPI 改成 480×320（半屏花屏）。面板保持厂商竖屏时序，
 * LVGL 按 480×320 绘制，flush 时用 PPA SRM 旋转写入 DPI 帧缓冲（硬件加速，非 CPU 软旋）。
 ******************************************************************************
 */

#include "lvgl_demo.h"
#include "led.h"
#include "lcd.h"
#include "touch.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_heap_caps.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "lv_mainstart.h"
#include "ui_pages.h"
#include "ui_theme.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "lv_main";

#define LV_DEMO_TASK_PRIO   8
#define LV_DEMO_STK_DEPTH   (16 * 1024)
#define LV_DEMO_TASK_CORE   1
#define LV_LOOP_MIN_MS      2
#define LV_LOOP_MAX_MS      12
#define LV_SHELL_PUMP_BURST 2

#if ST7796U_PPA_UI_LANDSCAPE
#define UI_FB_W  480u
#define UI_FB_H  320u
#endif

static TaskHandle_t s_lv_demo_task;
static lv_display_t *s_disp;

#if ST7796U_PPA_UI_LANDSCAPE
static ppa_client_handle_t s_ppa_srm;
static void *s_dpi_fb[2];
static size_t s_dpi_fb_bytes;
static int s_dpi_fb_idx;
static uint32_t s_phys_w;
static uint32_t s_phys_h;
static SemaphoreHandle_t s_vsync_sem;

/*
 * esp_lcd_dpi_panel_t 头部布局（IDF 6.0.2）。draw_bitmap(DPI FB) 会 C2M 踩掉 PPA 结果，
 * 故只改 cur_fb_index 切帧；GDMA 下一轮会扫新 FB。
 */
typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_dsi_bus_handle_t bus;
    uint8_t virtual_channel;
    uint8_t cur_fb_index;
} dpi_panel_fb_head_t;

static inline void dpi_flip_fb(esp_lcd_panel_handle_t panel, uint8_t idx)
{
    dpi_panel_fb_head_t *dpi = __containerof(panel, dpi_panel_fb_head_t, base);
    dpi->cur_fb_index = idx;
}
#endif

static void lv_demo_task(void *pvParameters);
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static bool touchpad_is_pressed(void);
static void touchpad_get_xy(int32_t *x, int32_t *y);

#if ST7796U_PPA_UI_LANDSCAPE
/* 帧扫完信号：切 FB 前等待，减轻物理顶部（横屏右侧）撕裂浅纹 */
IRAM_ATTR static bool on_refresh_done(esp_lcd_panel_handle_t panel,
                                      esp_lcd_dpi_panel_event_data_t *edata,
                                      void *user_ctx)
{
    (void)panel;
    (void)edata;
    (void)user_ctx;
    BaseType_t hp = pdFALSE;
    if (s_vsync_sem) {
        xSemaphoreGiveFromISR(s_vsync_sem, &hp);
    }
    return hp == pdTRUE;
}
#else
IRAM_ATTR static bool on_color_trans_done(esp_lcd_panel_handle_t panel,
                                          esp_lcd_dpi_panel_event_data_t *edata,
                                          void *user_ctx)
{
    (void)panel;
    (void)edata;
    lv_display_t *disp = (lv_display_t *)user_ctx;
    if (disp) {
        lv_display_flush_ready(disp);
    }
    return false;
}
#endif

static void lvgl_boot_test_frame(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x070B12), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_refr_now(s_disp);
#if ST7796U_PPA_UI_LANDSCAPE
    ESP_LOGI(TAG, "boot test frame OK (UI %ux%u → panel %ux%u PPA)",
             (unsigned)UI_FB_W, (unsigned)UI_FB_H,
             (unsigned)lcddev.width, (unsigned)lcddev.height);
#else
    ESP_LOGI(TAG, "boot test frame OK (%ux%u dir=%u)",
             (unsigned)lcddev.width, (unsigned)lcddev.height, (unsigned)lcddev.dir);
#endif
}

void lvgl_demo(void)
{
    led_init();

    lv_init();
    s_disp = lv_port_disp_init();
    lv_port_indev_init();

    lv_lock();
    lvgl_boot_test_frame();
    lv_unlock();

    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        lv_demo_task, "lv_demo_task", LV_DEMO_STK_DEPTH, NULL,
        LV_DEMO_TASK_PRIO, &s_lv_demo_task, LV_DEMO_TASK_CORE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "lv_demo_task 创建失败");
    } else {
        ESP_LOGI(TAG, "lv_demo_task OK（PSRAM 栈 %u words, prio %d）",
                 (unsigned)LV_DEMO_STK_DEPTH, LV_DEMO_TASK_PRIO);
    }
}

static void lv_demo_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "lv_demo_task 运行");

    lv_lock();
    lv_mainstart();
    lv_unlock();

    {
        const int intro_ms = 400;
        const int step_ms = 16;
        int left = intro_ms;
        TickType_t wake = xTaskGetTickCount();
        while (left > 0) {
            lv_lock();
            lv_tick_inc((uint32_t)step_ms);
            lv_timer_handler();
            lv_unlock();
            vTaskDelayUntil(&wake, pdMS_TO_TICKS(step_ms));
            left -= step_ms;
        }
    }

    lv_lock();
    ESP_LOGI(TAG, "开始加载 UI shell…");
    ui_shell_create();
    lv_unlock();

    {
        const int handoff_ms = 400;
        const int step_ms = 16;
        int left = handoff_ms;
        TickType_t wake = xTaskGetTickCount();
        while (left > 0) {
            lv_lock();
            lv_tick_inc((uint32_t)step_ms);
            (void)ui_shell_pump();
            lv_timer_handler();
            lv_unlock();
            vTaskDelayUntil(&wake, pdMS_TO_TICKS(step_ms));
            left -= step_ms;
        }
    }
    ESP_LOGI(TAG, "UI shell 就绪");

    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        TickType_t now = xTaskGetTickCount();
        uint32_t elapsed = (uint32_t)((now - last_wake) * portTICK_PERIOD_MS);
        if (elapsed == 0) {
            elapsed = 1;
        }
        last_wake = now;

        lv_lock();
        lv_tick_inc(elapsed);
        int pump_left = LV_SHELL_PUMP_BURST;
        while (pump_left-- > 0 && ui_shell_pump()) {
        }
        uint32_t sleep_ms = lv_timer_handler();
        lv_unlock();

        if (sleep_ms == LV_NO_TIMER_READY) {
            sleep_ms = LV_LOOP_MIN_MS;
        }
        if (sleep_ms > LV_LOOP_MAX_MS) {
            sleep_ms = LV_LOOP_MAX_MS;
        }
        if (sleep_ms < LV_LOOP_MIN_MS) {
            sleep_ms = LV_LOOP_MIN_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

lv_display_t *lv_port_disp_init(void)
{
    lcd_init_for_lvgl();

    esp_lcd_panel_handle_t panel = lcddev.lcd_panel_handle;
    const uint32_t phys_w = lcddev.width;
    const uint32_t phys_h = lcddev.height;
    const size_t bpp = lv_color_format_get_size(LV_COLOR_FORMAT_RGB565);

    void *fb0 = NULL;
    void *fb1 = NULL;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, 2, &fb0, &fb1));
    const size_t phys_bytes = (size_t)phys_w * (size_t)phys_h * bpp;
    memset(fb0, 0, phys_bytes);
    memset(fb1, 0, phys_bytes);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, (int)phys_w, (int)phys_h, fb0);

#if ST7796U_PPA_UI_LANDSCAPE
    s_phys_w = phys_w;
    s_phys_h = phys_h;
    s_dpi_fb[0] = fb0;
    s_dpi_fb[1] = fb1;
    s_dpi_fb_bytes = phys_bytes;
    s_dpi_fb_idx = 0;

    ppa_client_config_t ppa_cfg = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_cfg, &s_ppa_srm));

    s_vsync_sem = xSemaphoreCreateBinary();
    if (!s_vsync_sem) {
        ESP_LOGE(TAG, "vsync sem alloc failed");
        abort();
    }

    const uint32_t log_w = UI_FB_W;
    const uint32_t log_h = UI_FB_H;
    const size_t log_bytes = (size_t)log_w * (size_t)log_h * bpp;
    void *draw0 = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, log_bytes,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *draw1 = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, log_bytes,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!draw0 || !draw1) {
        ESP_LOGE(TAG, "PPA LVGL draw buf alloc failed (%u KB×2)", (unsigned)(log_bytes / 1024));
        abort();
    }
    ESP_LOGI(TAG, "draw buf aligned %u @ %p %p", (unsigned)LV_DRAW_BUF_ALIGN, draw0, draw1);

    lv_display_t *disp = lv_display_create((int32_t)log_w, (int32_t)log_h);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(disp, panel);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_buffers(disp, draw0, draw1, log_bytes, LV_DISPLAY_RENDER_MODE_FULL);

    const esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_refresh_done = on_refresh_done,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, NULL));

    ESP_LOGI(TAG, "LVGL FULL %ux%u + PPA→DPI flip %ux%u (no draw_bitmap C2M)",
             (unsigned)log_w, (unsigned)log_h,
             (unsigned)phys_w, (unsigned)phys_h);
    return disp;
#else
    lv_display_t *disp = lv_display_create((int32_t)phys_w, (int32_t)phys_h);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(disp, panel);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_buffers(disp, fb0, fb1, phys_bytes, LV_DISPLAY_RENDER_MODE_FULL);

    const esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, disp));

    ESP_LOGI(TAG, "LVGL FULL %ux%u DPI FB×2 (%u KB)",
             (unsigned)phys_w, (unsigned)phys_h, (unsigned)(phys_bytes / 1024));
    return disp;
#endif
}

lv_indev_t *lv_port_indev_init(void)
{
    tp_dev.init();

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
    lv_indev_set_display(indev, lv_display_get_default());

    return indev;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    (void)area;

#if ST7796U_PPA_UI_LANDSCAPE
    /*
     * PPA 直写离屏 DPI FB → vsync → 只改 cur_fb_index。
     * 禁止 draw_bitmap(DPI FB)：其 C2M 会把 CPU 陈旧 cache 盖回 PSRAM，出现浅横纹。
     */
    const uint8_t next = (uint8_t)(s_dpi_fb_idx ^ 1);
    void *out = s_dpi_fb[next];
    ppa_srm_oper_config_t srm = {
        .in = {
            .buffer = px_map,
            .pic_w = UI_FB_W,
            .pic_h = UI_FB_H,
            .block_w = UI_FB_W,
            .block_h = UI_FB_H,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = out,
            .buffer_size = s_dpi_fb_bytes,
            .pic_w = s_phys_w,
            .pic_h = s_phys_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_270,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa_srm, &srm);
    if (err != ESP_OK) {
        static int s_ppa_err_n;
        if (s_ppa_err_n++ < 3) {
            ESP_LOGE(TAG, "PPA rotate failed: %s", esp_err_to_name(err));
        }
        lv_display_flush_ready(disp);
        return;
    }

    /* 丢掉该 FB 上可能的 CPU cache，避免别处误 C2M；显示走 GDMA 读 PSRAM */
    esp_cache_msync(out, s_dpi_fb_bytes,
                    ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

    while (xSemaphoreTake(s_vsync_sem, 0) == pdTRUE) {
    }
    (void)xSemaphoreTake(s_vsync_sem, pdMS_TO_TICKS(50));

    s_dpi_fb_idx = next;
    dpi_flip_fb(panel, next);
    lv_display_flush_ready(disp);
#else
    esp_lcd_panel_draw_bitmap(panel, 0, 0, (int)lcddev.width, (int)lcddev.height, px_map);
#endif
}

static bool touchpad_is_pressed(void)
{
    tp_dev.scan(0);
    return (tp_dev.sta & TP_PRES_DOWN) ? true : false;
}

static void touchpad_get_xy(int32_t *x, int32_t *y)
{
#if ST7796U_PPA_UI_LANDSCAPE
    /* 物理 320×480 → 逻辑 480×320（与 PPA 270°CCW 一致） */
    const int32_t px = tp_dev.x[0];
    const int32_t py = tp_dev.y[0];
    *x = py;
    *y = (int32_t)lcddev.width - 1 - px;
#else
    *x = tp_dev.x[0];
    *y = tp_dev.y[0];
#endif
}

void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int32_t last_x = 0;
    static int32_t last_y = 0;

    (void)indev;

    if (touchpad_is_pressed()) {
        touchpad_get_xy(&last_x, &last_y);
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}
