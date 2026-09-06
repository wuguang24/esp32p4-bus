/**
 ******************************************************************************
 * @file        sdmmc.h
 * @version     V1.0
 * @brief       TF驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

#ifndef __SDMMC_H
#define __SDMMC_H

#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ldo_regulator.h"

/* 设置VDD_SD_DPHY输出电压 */
#define SD_PHY_PWR_LDO_CHAN       4       /* LDO_VO4 连接 VDD_SD_DPHY */
#define SD_PHY_PWR_LDO_VOLTAGE_MV 3300    /* 输出3.3V给到SDIO */

/* SDMMC外设相关硬件引脚 */
#define SD_PWR_EN_GPIO      GPIO_NUM_45  
#define SDMMC_PIN_CMD       GPIO_NUM_44  
#define SDMMC_PIN_CLK       GPIO_NUM_43 
#define SDMMC_PIN_D0        GPIO_NUM_39  
#define SDMMC_PIN_D1        GPIO_NUM_40 
#define SDMMC_PIN_D2        GPIO_NUM_41  
#define SDMMC_PIN_D3        GPIO_NUM_42  

/* 挂载点定义 */
#define MOUNT_POINT         "/sdcard"

/* 外部变量声明 */
extern sdmmc_card_t *card;
extern uint8_t sdmmc_mount_flag;

/* 函数声明 */
esp_err_t sdmmc_init(void);               /* SD卡初始化并挂载 */
esp_err_t sdmmc_unmount(void);            /* 取消挂载SD卡 */
void sd_dev_bsp_enable_phy_power(void);   /* 配置SD phy电压 */

#endif