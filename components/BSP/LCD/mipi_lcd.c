/**
 ******************************************************************************
 * @file        mipi_lcd.c
 * @version     V1.0
 * @brief       mipilcd驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

#include "mipi_lcd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *mipi_lcd_tag = "mipi_lcd";
uint8_t mipi_id[3];     /* 存放MIPI屏驱动IC的ID */
_mipilcd_dev mipidev;   /* 管理MIPI重要参数 */

/**
 * @brief       配置mipi phy电压
 * @param       无
 * @retval      无
 */
void mipi_dev_bsp_enable_dsi_phy_power(void)
{
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
#ifdef MIPI_DSI_PHY_PWR_LDO_CHAN
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id    = MIPI_DSI_PHY_PWR_LDO_CHAN,        /* 选择内存LDO */
        .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,  /* 输出标准电压提供VDD_MIPI_DPHY */
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_LOGI(mipi_lcd_tag, "MIPI DSI PHY Powered on");
#endif
}

/**
 * @brief       删除LCD面板
 * @param       panel:LCD接口句柄
 * @retval      ESP_OK:删除成功
 */
static esp_err_t mipi_lcd_paneldel(esp_lcd_panel_t *panel)
{
    mipi_panel_t *mipi_lcd = __containerof(panel, mipi_panel_t, base);
    if (mipi_lcd->reset_gpio_num >= 0)
    {
        gpio_reset_pin(mipi_lcd->reset_gpio_num);
    }

    free(mipi_lcd);

    return ESP_OK;
}

/**
 * @brief       复位LCD面板
 * @param       panel:LCD接口句柄
 * @retval      ESP_OK:复位成功
 */
static esp_err_t mipi_lcd_panelreset(esp_lcd_panel_t *panel)
{
    mipi_panel_t *mipi_lcd = __containerof(panel, mipi_panel_t, base);
    esp_lcd_panel_io_handle_t io = mipi_lcd->io;

    /* 如果有GPIO控制LCD的复位管脚 */
    if (mipi_lcd->reset_gpio_num >= 0)      /* 硬件复位 */
    {
        gpio_set_level(mipi_lcd->reset_gpio_num, mipi_lcd->reset_level);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(mipi_lcd->reset_gpio_num, !mipi_lcd->reset_level);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    else                                    /* 软件复位 */
    { 
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), mipi_lcd_tag, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return ESP_OK;
}

/**
 * @brief       初始化LCD面板
 * @param       panel:LCD接口句柄
 * @retval      ESP_OK:复位成功
 */
static esp_err_t mipi_lcd_panelinit(esp_lcd_panel_t *panel)
{
    mipi_panel_t *mipi_lcd = __containerof(panel, mipi_panel_t, base);
    esp_lcd_panel_io_handle_t io = mipi_lcd->io;
    
    const mipi_lcd_init_cmd_t *init_cmds = {0};
    uint16_t init_cmds_size = 0;
    bool mirror_x;
	bool mirror_y;
	
	if (mipidev.id == 0x9881d)
	{
		mirror_x = true;
        mirror_y = true;
	}
	else
	{
		mirror_x = false;
        mirror_y = false;
	}

    if (mipidev.id == 0x7796)   /* 3.5寸,320x480 ST7796U */
    {
        LCD_RST(1);
        vTaskDelay(pdMS_TO_TICKS(1));
        LCD_RST(0);
        vTaskDelay(pdMS_TO_TICKS(10));
        LCD_RST(1);
        vTaskDelay(pdMS_TO_TICKS(200));

#if ST7796U_LCD_LANDSCAPE
        init_cmds = st7796u_init_cmds_landscape;
        init_cmds_size = ST7796U_INIT_CMD_LANDSCAPE_LEN;
#else
        init_cmds = st7796u_init_cmds;
        init_cmds_size = ST7796U_INIT_CMD_LEN;
#endif
    }
	else if (mipidev.id == 0x7703)       /* 5.5寸,720P ST7703*/
    {
		init_cmds = st7703_init_cmds;
        init_cmds_size = ST7703_INIT_CMD_LEN;
	}
	else if (mipidev.id == 0x79007)   /* 7寸,1024x600 EK79007 */
    {
		LCD_RST(1);
		vTaskDelay(pdMS_TO_TICKS(5));
		LCD_RST(0);
		vTaskDelay(pdMS_TO_TICKS(10));
		LCD_RST(1);
		vTaskDelay(pdMS_TO_TICKS(120));
   
        init_cmds = ek79007_init_cmds;
        init_cmds_size = EK79007_INIT_CMD_LEN;
    }
    else if (mipidev.id == 0x9881c8 || mipidev.id == 0x9881c10 ||mipidev.id == 0x9881d)
	{
		if(mipidev.id == 0x9881c8)        /* 8寸,800P */
		{
			init_cmds = ili9881c80_init_cmds;
            init_cmds_size = ILI9881C80_INIT_CMD_LEN;
		}
		else if(mipidev.id == 0x9881c10)   /* 10.1寸,800P */
		{
			init_cmds = ili9881c101_init_cmds;
            init_cmds_size = ILI9881C101_INIT_CMD_LEN;
		}
		else if(mipidev.id == 0x9881d)     /* 5寸,720P */
		{
			init_cmds = ili9881d_init_cmds;
            init_cmds_size = ILI9881D_INIT_CMD_LEN;
		}

        /* 返回 命令页 1 */
		ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ILI9881_CMD_CNDBKxSEL, (uint8_t[]) {
			ILI9881_CMD_BKxSEL_BYTE0, ILI9881_CMD_BKxSEL_BYTE1, ILI9881_CMD_BKxSEL_BYTE2_PAGE1
		}, 3), mipi_lcd_tag, "send command failed");

        /* 设置2 lane */
		ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ILI9881_PAD_CONTROL, (uint8_t[]) {
			ILI9881_DSI_2_LANE,
		}, 1), mipi_lcd_tag, "send command failed");

		/* 返回 命令页 0 */
		ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ILI9881_CMD_CNDBKxSEL, (uint8_t[]) {
			ILI9881_CMD_BKxSEL_BYTE0, ILI9881_CMD_BKxSEL_BYTE1, ILI9881_CMD_BKxSEL_BYTE2_PAGE0
		}, 3), mipi_lcd_tag, "send command failed");
	}
    
    /* 发送初始化序列 */
    for (int i = 0; i < init_cmds_size; i++)
    {
        if (init_cmds[i].cmd == 0x11) 
        {
            ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), 
                              mipi_lcd_tag, "send command failed");
            if (mipidev.id == 0x7703)
			{
                vTaskDelay(pdMS_TO_TICKS(250));  
			}
			else if (mipidev.id == 0x9881c8 || mipidev.id == 0x9881c10 || mipidev.id == 0x9881d)
			{
				vTaskDelay(pdMS_TO_TICKS(120)); 
			}		 
			else if (mipidev.id == 0x7796)   
            {
                vTaskDelay(pdMS_TO_TICKS(200));
            } 
        }
        else if (init_cmds[i].cmd == 0x29)  
        {
            ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), 
                              mipi_lcd_tag, "send command failed");
            if (mipidev.id == 0x7703)
			{
                vTaskDelay(pdMS_TO_TICKS(50));  
			}
			else if (mipidev.id == 0x9881c8 || mipidev.id == 0x9881c10 || mipidev.id == 0x9881d)
			{
				vTaskDelay(pdMS_TO_TICKS(120)); 
			}	
			else if (mipidev.id == 0x7796)   /* ST7796U的0x29命令后可能不需要特殊延时 */
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
		else if (init_cmds[i].cmd == 0xFF && mipidev.id == 0x7703)
        {
            if (init_cmds[i].data_bytes > 0) {
                uint8_t delay_type = ((uint8_t*)init_cmds[i].data)[0];
                if (delay_type == 0x00) {
                    vTaskDelay(pdMS_TO_TICKS(250)); 
                } else if (delay_type == 0x01) {
                    vTaskDelay(pdMS_TO_TICKS(50));  
                }
            }
        }
        else
        {
            ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), 
                              mipi_lcd_tag, "send command failed");
        }
    }

    /* 只有不是ST7796U时才执行镜像设置 */
    if (mipidev.id != 0x7796)
	{
		/* 根据MIPI屏放置位置调整显示方向 */
		if (mirror_x)
		{
			mipi_lcd->madctl_val |= HX_F_SS_PANEL;      /* 扫描方向水平翻转 */
		}
		else
		{
			mipi_lcd->madctl_val &= ~HX_F_SS_PANEL;     /* 扫描方向水平不翻转 */
		}

		if (mirror_y)
		{
			mipi_lcd->madctl_val |= HX_F_GS_PANEL;      /* 扫描方向垂直翻转 */
		}
		else
		{
			mipi_lcd->madctl_val &= ~HX_F_GS_PANEL;     /* 扫描方向垂直不翻转 */
		}
		
		ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[])
		{
			mipi_lcd->madctl_val,
		}, 1), mipi_lcd_tag, "send command failed");    /* 配置MIPILCD的显示 */
	}
    
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD, (uint8_t[])
    {
        mipi_lcd->colmod_val,
    }, 1), mipi_lcd_tag, "send command failed");    /* 配置像素格式 */

    return ESP_OK;
}

/**
 * @brief       反转显示
 * @param       panel             :LCD接口句柄
 * @param       invert_color_data :true反转显示;false正常显示
 * @retval      ESP_OK:复位成功
 */
static esp_err_t mipi_lcd_panelinvert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    mipi_panel_t *mipi_lcd = __containerof(panel, mipi_panel_t, base);
    esp_lcd_panel_io_handle_t io = mipi_lcd->io;
    int command = 0;

    if (invert_color_data)
    {
        command = LCD_CMD_INVON;    /* 反转显示打开 */
    }
    else
    {
        command = LCD_CMD_INVOFF;   /* 反转显示关闭 */
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), mipi_lcd_tag, "send command failed");
    
    return ESP_OK;
}

/**
 * @brief       在特定轴上镜像LCD面板
 * @param       panel       :LCD接口句柄
 * @param       mirror_x    :true对x轴进行镜像,false不操作镜像
 * @param       mirror_y    :true对y轴进行镜像,false不进行镜像
 * @retval      ESP_OK:复位成功
 */
static esp_err_t mipi_lcd_panelmirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    mipi_panel_t *mipi_lcd = __containerof(panel, mipi_panel_t, base);
    esp_lcd_panel_io_handle_t io = mipi_lcd->io;

    if (mirror_x)
    {
        mipi_lcd->madctl_val |= HX_F_SS_PANEL;      /* 扫描方向水平翻转(镜像x) */
    }
    else
    {
        mipi_lcd->madctl_val &= ~HX_F_SS_PANEL;     /* 扫描方向水平不翻转 */
    }

    if (mirror_y)
    {
        mipi_lcd->madctl_val |= HX_F_GS_PANEL;      /* 扫描方向垂直翻转(镜像y) */
    }
    else
    {
        mipi_lcd->madctl_val &= ~HX_F_GS_PANEL;     /* 扫描方向垂直不翻转 */
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[])
    {
        mipi_lcd->madctl_val,
    }, 1), mipi_lcd_tag, "send command failed");    /* 配置MIPILCD的显示 */

    return ESP_OK;
}

/**
 * @brief       交换x和y轴
 * @param       panel       :LCD接口句柄
 * @param       swap_axes   :是否对x和y轴进行方向互换
 * @retval      ESP_ERR_NOT_SUPPORTED:适配的MIPILCD不支持
 */
static esp_err_t mipi_lcd_panelswap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    ESP_LOGW(mipi_lcd_tag, "Swap XY is not supported in ST7703/ILI9881C driver. Please use SW rotation.");
    return ESP_ERR_NOT_SUPPORTED;
}

/**
 * @brief       设置x轴和y轴的偏移
 * @param       panel :LCD接口句柄
 * @param       x_gap :X方向偏移
 * @param       y_gap :Y方向偏移
 * @retval      ESP_OK:设置成功
 */
static esp_err_t mipi_lcd_panelset_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    mipi_panel_t *mipi_lcd = __containerof(panel, mipi_panel_t, base);
    mipi_lcd->x_gap = x_gap;
    mipi_lcd->y_gap = y_gap;
    return ESP_OK;
}

/**
 * @brief       打开或者关闭显示
 * @param       panel :LCD接口句柄
 * @param       on_off:true打开显示,flase关闭显示
 * @retval      ESP_OK:设置成功
 */
static esp_err_t mipi_lcd_paneldisp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    mipi_panel_t *mipi_lcd = __containerof(panel, mipi_panel_t, base);
    esp_lcd_panel_io_handle_t io = mipi_lcd->io;
    int command = 0;

    if (on_off)
    {
        command = LCD_CMD_DISPON;   /* 打开显示命令 */
    }
    else
    {
        command = LCD_CMD_DISPOFF;  /* 关闭显示命令 */
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), mipi_lcd_tag, "send command failed");
    
    return ESP_OK;
}

/**
 * @brief       进入或退出睡眠模式
 * @param       sleep :true进入睡眠模式,flase:退出睡眠模式
 * @retval      ESP_OK:设置成功
 */
static esp_err_t mipi_lcd_panelsleep(esp_lcd_panel_t *panel, bool sleep)
{
    mipi_panel_t *mipi_lcd = __containerof(panel, mipi_panel_t, base);
    esp_lcd_panel_io_handle_t io = mipi_lcd->io;
    int command = 0;

    if (sleep)
    {
        command = LCD_CMD_SLPIN;    /* 进入睡眠模式命令 */
    }
    else
    {
        command = LCD_CMD_SLPOUT;   /* 退出睡眠模式命令 */
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), mipi_lcd_tag, "io tx param failed");
    
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}

/**
 * @brief       新建mipilcd句柄，并配置函数
 * @param       io              :MIPI IO句柄
 * @param       panel_dev_config:MIPI 设备配置结构体
 * @param       ret_panel       :MIPI屏句柄
 * @retval      esp_err_t 返回值
 */
esp_err_t mipi_lcd_new_panel(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, mipi_lcd_tag, "invalid argument");

    mipi_panel_t *mipi_lcd = (mipi_panel_t *)calloc(1, sizeof(mipi_panel_t));
    ESP_RETURN_ON_FALSE(mipi_lcd, ESP_ERR_NO_MEM, mipi_lcd_tag, "no mem for mipi_lcd panel");

    if (panel_dev_config->reset_gpio_num >= 0)      /* 配置LCD复位引脚 */
    {
        gpio_config_t io_conf = {
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, mipi_lcd_tag, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order)        /* 颜色顺序RGB/BGR */
    {
        case LCD_RGB_ELEMENT_ORDER_RGB:
            mipi_lcd->madctl_val = 0;
            break;
        case LCD_RGB_ELEMENT_ORDER_BGR:
            mipi_lcd->madctl_val |= LCD_CMD_BGR_BIT;
            break;
        default:
            ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, mipi_lcd_tag, "unsupported rgb element order");
            break;
    }

    switch (panel_dev_config->bits_per_pixel)       /* 像素格式 */
    {
        case 16:    /* RGB565 */
            mipi_lcd->colmod_val = 0x55;
            break;
        case 18:    /* RGB666 */
            mipi_lcd->colmod_val = 0x66;
            break;
        case 24:    /* RGB888 */
            mipi_lcd->colmod_val = 0x77;
            break;
        default:
            ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, mipi_lcd_tag, "unsupported pixel width");
            break;
    }

    mipi_lcd->io                = io;
    mipi_lcd->reset_gpio_num    = panel_dev_config->reset_gpio_num;         /* LCD的复位引脚 */
    mipi_lcd->reset_level       = panel_dev_config->flags.reset_active_high;/* 复位引脚的有效电平 */
    mipi_lcd->base.reset        = mipi_lcd_panelreset;                      /* 复位LCD面板 */
    mipi_lcd->base.init         = mipi_lcd_panelinit;                       /* 初始化LCD面板 */
    mipi_lcd->base.del          = mipi_lcd_paneldel;                        /* 删除LCD面板 */
    mipi_lcd->base.mirror       = mipi_lcd_panelmirror;                     /* 在特定轴上镜像LCD面板 */
    mipi_lcd->base.swap_xy      = mipi_lcd_panelswap_xy;                    /* 交换x轴和y轴 */
    mipi_lcd->base.set_gap      = mipi_lcd_panelset_gap;                    /* 设置x轴和y轴的偏移 */
    mipi_lcd->base.invert_color = mipi_lcd_panelinvert_color;               /* 反转显示 */
    mipi_lcd->base.disp_on_off  = mipi_lcd_paneldisp_on_off;                /* 打开或关闭显示 */
    mipi_lcd->base.disp_sleep   = mipi_lcd_panelsleep;                      /* 进入或退出睡眠模式 */

    *ret_panel = &mipi_lcd->base;

    return ESP_OK;

err:
    if (mipi_lcd)
    {
        mipi_lcd_paneldel(&mipi_lcd->base);
    }

    return ret;
}

/**
 * @brief       mipi_lcd初始化
 * @param       无
 * @retval      LCD控制句柄
 */
esp_lcd_panel_handle_t mipi_lcd_init(void)
{
    mipi_dev_bsp_enable_dsi_phy_power();                    /* 配置DSI接口电压2.5V */
    
    /* 创建DSI总线 */
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id             = 0,                            /* 总线ID */
        .num_data_lanes     = MIPI_DSI_LANE_NUM,            /* 2路数据信号 */
        .phy_clk_src        = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT, /* DPHY时钟源(XTAL晶振) */
        .lane_bit_rate_mbps = MIPI_DSI_LANE_BITRATE_MBPS,   /* 数据通道比特率(Mbps) */
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));   /* 新建DSI总线 */

    /* 配置DSI总线的DBI接口 */
    esp_lcd_panel_io_handle_t mipi_dbi_io;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,                               /* 虚拟通道(只有一个LCD连接,设置0即可) */
        .lcd_cmd_bits    = 8,                               /* 根据MIPI LCD驱动IC规格设置 命令位宽度 */
        .lcd_param_bits  = 8,                               /* 根据MIPI LCD驱动IC规格设置 参数位宽度 */
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

    /* 创建LCD控制器驱动 */
#if MIPILCD_35_480320_ST7796U
	esp_lcd_panel_handle_t mipi_lcd_ctrl_panel;             /* MIPI控制句柄 */
	esp_lcd_panel_dev_config_t lcd_dev_config = {
		.bits_per_pixel = 16,                               /* MIPILCD的像素位宽度 */
		.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,         /* 像素数据的BGR元素顺序,根据实际色彩情况选择BGR或RGB */
		.reset_gpio_num = lcddev.ctrl.lcd_rst,              /* MIPILCD屏的复位引脚 */
	};
#else
	esp_lcd_panel_handle_t mipi_lcd_ctrl_panel;             /* MIPI控制句柄 */
	esp_lcd_panel_dev_config_t lcd_dev_config = {
		.bits_per_pixel = 16,                               /* MIPILCD的像素位宽度 */
		.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,         /* 像素数据的RGB元素顺序,根据实际色彩情况选择BGR或RGB */
		.reset_gpio_num = lcddev.ctrl.lcd_rst,              /* MIPILCD屏的复位引脚 */
	};
#endif

    ESP_ERROR_CHECK(mipi_lcd_new_panel(mipi_dbi_io, &lcd_dev_config, &mipi_lcd_ctrl_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(mipi_lcd_ctrl_panel));              /* 复位MIPILCD屏 */

	mipidev.id = mipi_lcd_read_id(mipi_dbi_io);                             /* 读取MIPILCD的ID */
    ESP_LOGI(mipi_lcd_tag, "mipilcd_id:%#x ", mipidev.id);                  /* 打印MIPILCD的ID */

    ESP_ERROR_CHECK(esp_lcd_panel_init(mipi_lcd_ctrl_panel));               /* 初始化MIPILCD屏 */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(mipi_lcd_ctrl_panel, true));  /* 打开MIPILCD屏 */

    if (mipidev.id == 0x7796)                                    /* 3.5寸 ST7796U */
    {
        /* 竖屏已验证 porch；横屏仅改分辨率+MADCTL，禁止对调 h/v（对调会 MIPI 失锁黑屏） */
        mipidev.hbp      = 200;
        mipidev.hfp      = 200;
        mipidev.hsw      = 80;
        mipidev.vbp      = 6;
        mipidev.vfp      = 6;
        mipidev.vsw      = 8;
        mipidev.pclk_mhz = 24;
#if ST7796U_LCD_LANDSCAPE
        mipidev.pwidth   = 480;
        mipidev.pheight  = 320;
        mipidev.dir      = 1;
        ESP_LOGI(mipi_lcd_tag, "ST7796U 硬件横屏 480×320 MADCTL=0x68（porch 同竖屏）");
#else
        mipidev.pwidth   = 320;
        mipidev.pheight  = 480;
        mipidev.dir      = 0;
        ESP_LOGI(mipi_lcd_tag, "ST7796U 竖屏 320×480 MADCTL=0x48");
#endif
    }
    else if (mipidev.id == 0x9881d)                              /* 5寸720P ILI9881D屏幕参数 */
    {
		mipidev.pwidth   = 720;                                  /* 面板宽度,单位:像素 */
		mipidev.pheight  = 1280;                                 /* 面板高度,单位:像素 */
		mipidev.hbp      = 52;                                   /* 水平后廊 */
		mipidev.hfp      = 48;                                   /* 水平前廊 */
		mipidev.hsw      = 8;                                    /* 水平同步宽度 */
		mipidev.vbp      = 15;                                   /* 垂直后廊 */
		mipidev.vfp      = 16;                                   /* 垂直前廊 */
		mipidev.vsw      = 5;                                    /* 垂直同步宽度 */
		mipidev.pclk_mhz = 60;                                   /* 设置像素时钟 60Mhz */
		mipidev.dir      = 0;                                    /* 只能竖屏 */
	}
	else if (mipidev.id == 0x7703)                               /* 5.5寸720P ST7703屏幕参数 */
    {
		mipidev.pwidth   = 720;                                  /* 面板宽度,单位:像素 */
		mipidev.pheight  = 1280;                                 /* 面板高度,单位:像素 */
		mipidev.hbp      = 52;                                   /* 水平后廊 */
		mipidev.hfp      = 48;                                   /* 水平前廊 */
		mipidev.hsw      = 8;                                    /* 水平同步宽度 */
		mipidev.vbp      = 15;                                   /* 垂直后廊 */
		mipidev.vfp      = 16;                                   /* 垂直前廊 */
		mipidev.vsw      = 5;                                    /* 垂直同步宽度 */
		mipidev.pclk_mhz = 60;                                   /* 设置像素时钟 60Mhz */
		mipidev.dir      = 0;                                    /* 只能竖屏 */
	}
	else if (mipidev.id == 0x79007)                              /* 7寸1024x600 EK79007屏幕参数 */
    {
        mipidev.pwidth   = 1024;                                 /* 面板宽度,单位:像素 */
        mipidev.pheight  = 600;                                  /* 面板高度,单位:像素 */
        mipidev.hbp      = 160;                                  /* 水平后廊 */
        mipidev.hfp      = 160;                                  /* 水平前廊 */
        mipidev.hsw      = 20;                                   /* 水平同步宽度 */
        mipidev.vbp      = 23;                                   /* 垂直后廊 */
        mipidev.vfp      = 12;                                   /* 垂直前廊 */
        mipidev.vsw      = 10;                                   /* 垂直同步宽度 */
        mipidev.pclk_mhz = 50;                                   /* 设置像素时钟 50Mhz */
        mipidev.dir      = 1;                                    /* 只能横屏 */
    }
	else if (mipidev.id == 0x9881c8)                             /* 8寸800P ILI9881C屏幕参数 */
    {
		mipidev.pwidth   = 800;                                  /* 面板宽度,单位:像素 */
        mipidev.pheight  = 1280;                                 /* 面板高度,单位:像素 */
        mipidev.hbp      = 24;                                   /* 水平后廊 */
        mipidev.hfp      = 60;                                   /* 水平前廊 */
        mipidev.hsw      = 24;                                   /* 水平同步宽度 */
        mipidev.vbp      = 9;                                    /* 垂直后廊 */
        mipidev.vfp      = 7;                                    /* 垂直前廊 */
        mipidev.vsw      = 2;                                    /* 垂直同步宽度 */
        mipidev.pclk_mhz = 60;                                   /* 设置像素时钟 60Mhz */
        mipidev.dir      = 0;                                    /* 只能竖屏 */
	}
	else if (mipidev.id == 0x9881c10)                            /* 10.1寸800P ILI9881C屏幕参数 */
    {
		mipidev.pwidth   = 800;                                  /* 面板宽度,单位:像素 */
        mipidev.pheight  = 1280;                                 /* 面板高度,单位:像素 */
        mipidev.hbp      = 24;                                   /* 水平后廊 */
        mipidev.hfp      = 60;                                   /* 水平前廊 */
        mipidev.hsw      = 24;                                   /* 水平同步宽度 */
        mipidev.vbp      = 9;                                    /* 垂直后廊 */
        mipidev.vfp      = 7;                                    /* 垂直前廊 */
        mipidev.vsw      = 2;                                    /* 垂直同步宽度 */
        mipidev.pclk_mhz = 60;                                   /* 设置像素时钟 60Mhz */
        mipidev.dir      = 0;                                    /* 只能竖屏 */
	}

    lcddev.id     = mipidev.id;                              /* LCD_ID */
    lcddev.width  = mipidev.pwidth;                          /* 宽度 */
    lcddev.height = mipidev.pheight;                         /* 高度 */
    lcddev.dir    = mipidev.dir;                             /* 0竖屏 1横屏 */
    lcddev.lcd_dbi_io = mipi_dbi_io;                         /* LCD IO控制句柄 */
    
    esp_lcd_panel_handle_t mipi_dpi_panel;                   /* MIPILCD控制句柄 */
    esp_lcd_dpi_panel_config_t dpi_config = {                /* DSI数据配置 */
        .virtual_channel    = 0,                             /* 虚拟通道 */
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,  /* 时钟源 */
        .dpi_clock_freq_mhz = mipidev.pclk_mhz,              /* 像素时钟频率 */
        .in_color_format    = LCD_COLOR_FMT_RGB565,          /* 颜色格式（帧缓冲区输入） */
        .out_color_format   = LCD_COLOR_FMT_RGB565,          /* 颜色格式（面板输出） */
        .num_fbs            = 2,                             /* 帧缓冲区数量 */
        .video_timing       = {                              /* LCD面板特定时序参数 */
            .h_size            = mipidev.pwidth,             /* 水平分辨率,即一行中的像素数 */
            .v_size            = mipidev.pheight,            /* 垂直分辨率,即帧中的行数 */
            .hsync_back_porch  = mipidev.hbp,                /* 水平后廊,hsync和行活动数据开始之间的PCLK数 */
            .hsync_pulse_width = mipidev.hsw,                /* 水平同步宽度,单位:PCLK周期*/
            .hsync_front_porch = mipidev.hfp,                /* 水平前廊,活动数据结束和下一个hsync之间的PCLK数 */
            .vsync_back_porch  = mipidev.vbp,                /* 垂直后廊,vsync和帧开始之间的无效行数 */
            .vsync_pulse_width = mipidev.vsw,                /* 垂直同步宽度,单位:行数 */
            .vsync_front_porch = mipidev.vfp,                /* 垂直前廊,帧结束和下一个vsync之间的无效行数 */
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_config, &mipi_dpi_panel));     /* 为MIPI DSI DPI接口创建LCD控制句柄 */
    ESP_ERROR_CHECK(esp_lcd_panel_init(mipi_dpi_panel));                                    /* 初始化MIPILCD */

#if ST7796U_LCD_LANDSCAPE
    if (mipidev.id == 0x7796) {
        const uint8_t madctl = 0x68; /* MV|MX|BGR，与 init 序列一致 */
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(mipi_dbi_io, LCD_CMD_MADCTL, &madctl, 1));
        ESP_LOGI(mipi_lcd_tag, "ST7796U MADCTL=0x68 已在 DPI 初始化后重发");
    }
#endif

    /* LVGL 全屏双缓冲与例程一致：不启用 DMA2D，draw_bitmap 同步刷帧 */
    ESP_LOGI(mipi_lcd_tag, "DPI panel ready (LVGL shared FB, no DMA2D)");

    return mipi_dpi_panel;
}

/**
 * @brief       读取LCD ID
 * @param       io: LCD IO控制句柄
 * @retval      LCD ID
 */
uint32_t mipi_lcd_read_id(esp_lcd_panel_io_handle_t io)
{
    uint8_t id_data[3] = {0};
    uint32_t lcd_id = 0;

/* 使用3.5寸 320*480 ST7796U MIPI屏幕 */
#if MIPILCD_35_480320_ST7796U
	return 0x7796;
#endif

/* 使用7寸 1024*600 EK79007 MIPI屏幕 */
#if MIPILCD_7_1024600_EK79007
    return 0x79007;
#endif

    /* 尝试读取ST7703 ID */
    esp_lcd_panel_io_rx_param(io, ST7703_CMD_READ_DISPLAY_ID, id_data, 3);
    
	if (id_data[0] == 0x38 && id_data[1] == 0x21 && id_data[2] == 0x1F) 
	{
		lcd_id = 0x7703;  
		ESP_LOGI(mipi_lcd_tag, "ST7703 LCD");
	} 
	else 
	{
		memset(id_data, 0, sizeof(id_data));
		/* 读取ILI9881D ID */
        esp_lcd_panel_io_tx_param(io, ILI9881_CMD_CNDBKxSEL, (uint8_t[]) {
            ILI9881_CMD_BKxSEL_BYTE0, ILI9881_CMD_BKxSEL_BYTE1, ILI9881_CMD_BKxSEL_BYTE2_PAGE6
        }, 3);
        esp_lcd_panel_io_rx_param(io, 0xF0, &id_data[0], 1);
        esp_lcd_panel_io_rx_param(io, 0xF1, &id_data[1], 1);
		esp_lcd_panel_io_rx_param(io, 0xF2, &id_data[2], 1);

        lcd_id = (uint32_t)(id_data[0] << 16) | (id_data[1] << 8) | id_data[2];

		if(lcd_id == 0x98810d)
		{
			lcd_id = 0x9881d;
		}
		else
		{
			memset(id_data, 0, sizeof(id_data));
			/* 读取ILI9881C ID */
            esp_lcd_panel_io_tx_param(io, ILI9881_CMD_CNDBKxSEL, (uint8_t[]) {
            ILI9881_CMD_BKxSEL_BYTE0, ILI9881_CMD_BKxSEL_BYTE1, ILI9881_CMD_BKxSEL_BYTE2_PAGE1
            }, 3);
			esp_lcd_panel_io_rx_param(io, 0x00, &id_data[0], 1);
			esp_lcd_panel_io_rx_param(io, 0x01, &id_data[1], 1);
			esp_lcd_panel_io_rx_param(io, 0x02, &id_data[2], 1);

		    lcd_id = (uint32_t)(id_data[0] << 16) | (id_data[1] << 8) | id_data[2];
			
			if(lcd_id == 0x98815c)
			{
				/* 使用10.1寸 800*1280 MIPI屏幕 */
				#if MIPILCD_101_8001280_ILI9881C
					lcd_id = 0x9881c10;
				#else
				    lcd_id = 0x9881c8;
				#endif
			}
		}
	}

    return lcd_id;
}
