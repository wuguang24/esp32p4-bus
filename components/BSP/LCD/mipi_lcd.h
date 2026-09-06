/**
 ******************************************************************************
 * @file        mipi_lcd.h
 * @version     V1.0
 * @brief       mipilcd驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

#ifndef __MIPI_LCD_H
#define __MIPI_LCD_H

#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lcd.h"
#include "esp_check.h"
#include <math.h>
#include <string.h>
#include "mipilcd_ex.h"

/*
 * 3.5寸 ST7796U 方向（本板 MIPI）：
 * - MADCTL/DPI 改成 480×320 会半屏花屏（时序与面板扫描不匹配），故面板必须保持竖屏。
 * - 横屏 UI 用 ST7796U_PPA_UI_LANDSCAPE：LVGL 480×320 + PPA 硬件旋转写入 320×480 DPI FB。
 * - 勿把 h/v porch 对调——会 MIPI 失锁黑屏。
 */
#define ST7796U_LCD_LANDSCAPE                   0   /* 面板 DPI/MADCTL：必须竖屏 */
#define ST7796U_PPA_UI_LANDSCAPE                1   /* LVGL 逻辑横屏 + PPA 旋转 */

/* 选择使用的屏幕尺寸 */
#define MIPILCD_35_480320_ST7796U                1     /* 3.5 寸 320*480    ST7796U  MIPI屏 */
#define MIPILCD_7_1024600_EK79007                0       /* 7   寸 1024*600   EK79007  MIPI屏 */
#define MIPILCD_101_8001280_ILI9881C             0       /* 10.1寸 800*1280   ILI9881C MIPI屏 */

/* MIPI DSI总线配置 */
#if MIPILCD_35_480320_ST7796U
	#define MIPI_DSI_LANE_NUM               1       /* 1个通道数据线 */
	#define MIPI_DSI_LANE_BITRATE_MBPS      560     /* 通道比特率 */
#else
	#define MIPI_DSI_LANE_NUM               2       /* 2个通道数据线 */
	#define MIPI_DSI_LANE_BITRATE_MBPS      700     /* 通道比特率 */
#endif

/* 设置VDD_MIPI_DPHY输出电压 */
#define MIPI_DSI_PHY_PWR_LDO_CHAN       3       /* LDO_VO3 连接 VDD_MIPI_DPHY */
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500    /* 输出2.5V给到MIPI屏 */

/* ILI9881 命令集 */
#define ILI9881_CMD_CNDBKxSEL                  (0xFF)
#define ILI9881_CMD_BKxSEL_BYTE0               (0x98)
#define ILI9881_CMD_BKxSEL_BYTE1               (0x81)
#define ILI9881_CMD_BKxSEL_BYTE2_PAGE0         (0x00)
#define ILI9881_CMD_BKxSEL_BYTE2_PAGE1         (0x01)
#define ILI9881_CMD_BKxSEL_BYTE2_PAGE2         (0x02)
#define ILI9881_CMD_BKxSEL_BYTE2_PAGE3         (0x03)
#define ILI9881_CMD_BKxSEL_BYTE2_PAGE4         (0x04)
#define ILI9881_CMD_BKxSEL_BYTE2_PAGE5         (0x05)
#define ILI9881_CMD_BKxSEL_BYTE2_PAGE6         (0x06)
#define ILI9881_PAD_CONTROL                    (0xB7)
#define ILI9881_DSI_2_LANE                     (0x03)
#define ILI9881_DSI_3_4_LANE                   (0x02)

/* ST7703 命令集 */
#define ST7703_CMD_READ_DISPLAY_ID             (0x04)

/* 显示控制相关定义 */
#define HX_F_GS_PANEL                           (1 << 0)    /* 垂直翻转 */
#define HX_F_SS_PANEL                           (1 << 1)    /* 水平翻转 */
#define HX_F_BGR_PANEL                          (1 << 3)    /* 颜色选择BGR / RGB */

/* 初始化屏幕结构体 */
typedef struct {
    esp_lcd_panel_t base;           /* LCD设备的基础接口函数 */
    esp_lcd_panel_io_handle_t io;   /* LCD设备的IO接口函数配置 */
    int reset_gpio_num;             /* 复位管脚 */
    int x_gap;                      /* x偏移 */
    int y_gap;                      /* y偏移 */
    uint8_t madctl_val;             /* 保存LCD CMD MADCTL寄存器的当前值 */
    uint8_t colmod_val;             /* 保存LCD_CMD_COLMOD寄存器的当前值 */
    uint16_t init_cmds_size;        /* 初始化序列大小 */
    bool reset_level;               /* 复位电平 */
} mipi_panel_t;

/* MIPI LCD重要参数集 */
typedef struct  
{
    uint32_t id;                    /* 屏幕ID */
    uint32_t pwidth;                /* MIPI面板的宽度,固定参数,不随显示方向改变 */
    uint32_t pheight;               /* MIPI面板的高度,固定参数,不随显示方向改变 */
    uint16_t hsw;                   /* 水平同步宽度 */
    uint16_t vsw;                   /* 垂直同步宽度 */
    uint16_t hbp;                   /* 水平后廊 */
    uint16_t vbp;                   /* 垂直后廊 */
    uint16_t hfp;                   /* 水平前廊 */
    uint16_t vfp;                   /* 垂直前廊  */
    uint8_t dir;                    /* 0,竖屏;1,横屏; */
    uint32_t pclk_mhz;              /* 设置像素时钟 */
} _mipilcd_dev; 

/* MIPI参数 */
extern _mipilcd_dev mipidev;        /* 管理MIPI屏幕的重要参数 */

/* 函数声明 */
esp_lcd_panel_handle_t mipi_lcd_init(void);     /* mipi_lcd初始化 */
void mipi_dev_bsp_enable_dsi_phy_power(void);   /* 配置mipi phy电压 */
esp_err_t mipi_lcd_new_panel(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);
uint32_t mipi_lcd_read_id(esp_lcd_panel_io_handle_t io);

#endif
