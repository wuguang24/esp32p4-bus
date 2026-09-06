/**
 ******************************************************************************
 * @file        gt9xxx.c
 * @version     V1.0
 * @brief       电容触摸屏-GT9xxx 驱动代码
 *   @note      GT系列电容触摸屏IC通用驱动,本代码支持: GT9147/GT911/GT928/GT1151/GT9271 等多种
 *              驱动IC, 这些驱动IC仅ID不一样, 具体代码基本不需要做任何修改即可通过本代码直接驱动
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

#include "gt9xxx.h"


const char * gt9xxx_tag = "gt9xxx";
i2c_master_dev_handle_t gt9xxx_handle = NULL;

/* 注意: 除了GT9271支持10点触摸之外, 其他触摸芯片只支持 5点触摸 */
uint8_t g_gt_tnum = 5;      /* 默认支持的触摸屏点数(5点触摸) */

/**
 * @brief       向gt9xxx写入数据
 * @param       reg : 起始寄存器地址
 * @param       buf : 数据缓缓存区
 * @param       len : 写数据长度
 * @retval      esp_err_t ：0, 成功; 1, 失败;
 */
esp_err_t gt9xxx_wr_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    esp_err_t ret;
    uint8_t *wr_buf = malloc(2 + len);

    if (wr_buf == NULL)
    {
        ESP_LOGE(gt9xxx_tag, "%s memory failed", __func__);
        return ESP_ERR_NO_MEM;      /* 分配内存失败 */
    }

    wr_buf[0] = reg >> 8;
    wr_buf[1] = reg & 0XFF;

    memcpy(wr_buf + 2, buf, len);     /* 拷贝数据至存储区当中 */

    ret = i2c_master_transmit(gt9xxx_handle, wr_buf, sizeof(wr_buf),-1);

    free(wr_buf);                      /* 发送完成释放内存 */

    return ret;
}

/**
 * @brief       从gt9xxx读出数据
 * @param       reg : 起始寄存器地址
 * @param       buf : 数据缓缓存区
 * @param       len : 读数据长度
 * @retval      esp_err_t ：0, 成功; 1, 失败;
 */
esp_err_t gt9xxx_rd_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t memaddr_buf[2];
    memaddr_buf[0]  = reg >> 8;
    memaddr_buf[1]  = reg & 0XFF;

    return i2c_master_transmit_receive(gt9xxx_handle, memaddr_buf, sizeof(memaddr_buf), buf, len, -1);
}

/**
 * @brief       初始化gt9xxx触摸屏
 * @param       无
 * @retval      0, 初始化成功; 1, 初始化失败;
 */
esp_err_t gt9xxx_init(void)
{
    uint8_t temp[5];

    /* 未调用myiic_init初始化IIC */
    if (touch_i2c_bus == NULL)
    {
        ESP_ERROR_CHECK(myiic_init());
    }

    i2c_device_config_t gt9xxx_i2c_dev_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,      /* 从机地址长度 */
        .scl_speed_hz    = IIC_SPEED_CLK,           /* 传输速率 */
        .device_address  = GT9XXX_DEV_ID,           /* 从机7位的地址 */
    };
    /* I2C总线上添加gt9xxx设备 */
    ESP_ERROR_CHECK(i2c_master_bus_add_device(touch_i2c_bus, &gt9xxx_i2c_dev_conf, &gt9xxx_handle));

    /* 以下是对CT_RST和CT_INT引脚做配置,同时会产生的时序决定了驱动IC的器件地址为0x14(某些芯片还会有另外一种时序,会使器件地址为0x5D) */
    gpio_config_t gpio_init_struct = {0};
    gpio_init_struct.intr_type    = GPIO_INTR_DISABLE;              /* 失能引脚中断 */
    gpio_init_struct.mode         = GPIO_MODE_OUTPUT;               /* 输出模式 */
    gpio_init_struct.pull_up_en   = GPIO_PULLUP_ENABLE;             /* 使能上拉 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;          /* 失能下拉 */
    gpio_init_struct.pin_bit_mask = 1ull << GT9XXX_RST_GPIO_PIN;    /* 设置的引脚的位掩码 */
    gpio_config(&gpio_init_struct);                                 /* 配置复位引脚 */

    gpio_init_struct.mode         = GPIO_MODE_INPUT;                /* 输入模式 */
    gpio_init_struct.pin_bit_mask = 1ull << GT9XXX_INT_GPIO_PIN;    /* 设置的引脚的位掩码 */
    gpio_config(&gpio_init_struct);                                 /* 配置中断引脚 */

    CT_RST(0);          /* 复位 */
    vTaskDelay(10);
    CT_RST(1);          /* 释放复位 */
    vTaskDelay(10);

    gpio_init_struct.mode         = GPIO_MODE_INPUT;                /* 输入输出模式 */
    gpio_init_struct.pull_up_en   = GPIO_PULLUP_DISABLE;            /* 失能上拉 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;          /* 失能下拉 */
    gpio_init_struct.pin_bit_mask = 1ull << GT9XXX_INT_GPIO_PIN;    /* 设置的引脚的位掩码 */
    gpio_config(&gpio_init_struct);                                 /* 配置中断引脚 */

    vTaskDelay(100);

    gt9xxx_rd_reg(GT9XXX_PID_REG, temp, 4);     /* 读取产品ID */
    temp[4] = 0;
    /* 判断一下是否是特定的触摸屏 */
    if (strcmp((char *)temp, "911") && strcmp((char *)temp, "9147") && strcmp((char *)temp, "1158") && strcmp((char *)temp, "9271") && strcmp((char *)temp, "928"))
    {
        return 1;   /* 若不是触摸屏用到的GT911/9147/1158/9271，则初始化失败，需硬件查看触摸IC型号以及查看时序函数是否正确 */
    }
    ESP_LOGI("GT9XXX", "CTP:%s", temp);         /* 打印触摸屏驱动IC的ID */                      
    
    if (strcmp((char *)temp, "9271") == 0)      /* ID==9271 / ID==928, 支持10点触摸 */
    {
        g_gt_tnum = 10;                         /* 支持10点触摸屏 */
    }

    temp[0] = 0X02;
    gt9xxx_wr_reg(GT9XXX_CTRL_REG, temp, 1);    /* 软复位GT9XXX */
    
    vTaskDelay(10);
    
    temp[0] = 0X00;
    gt9xxx_wr_reg(GT9XXX_CTRL_REG, temp, 1);    /* 结束复位, 进入读坐标状态 */

    return ESP_OK;
}

/* GT9XXX 10个触摸点(最多) 对应的寄存器表 */
const uint16_t GT9XXX_TPX_TBL[10] =
{
    GT9XXX_TP1_REG, GT9XXX_TP2_REG, GT9XXX_TP3_REG, GT9XXX_TP4_REG, GT9XXX_TP5_REG,
    GT9XXX_TP6_REG, GT9XXX_TP7_REG, GT9XXX_TP8_REG, GT9XXX_TP9_REG, GT9XXX_TP10_REG,
};

/**
 * @brief       扫描触摸屏(采用查询方式)
 * @param       mode : 电容屏未用到次参数, 为了兼容电阻屏
 * @retval      当前触屏状态
 *   @arg       0, 触屏无触摸; 
 *   @arg       1, 触屏有触摸;
 */
uint8_t gt9xxx_scan(uint8_t mode)
{
    uint8_t buf[4];
    uint8_t i = 0;
    uint8_t res = 0;
    uint16_t temp;
    uint16_t tempsta;

    gt9xxx_rd_reg(GT9XXX_GSTID_REG, &mode, 1);

    if ((mode & 0X80) && ((mode & 0XF) <= g_gt_tnum)) {
        i = 0;
        gt9xxx_wr_reg(GT9XXX_GSTID_REG, &i, 1);
    }

    if ((mode & 0XF) && ((mode & 0XF) <= g_gt_tnum)) {
        temp = 0XFFFF << (mode & 0XF);
        tempsta = tp_dev.sta;
        tp_dev.sta = (~temp) | TP_PRES_DOWN | TP_CATH_PRES;
        tp_dev.x[g_gt_tnum - 1] = tp_dev.x[0];
        tp_dev.y[g_gt_tnum - 1] = tp_dev.y[0];

        for (i = 0; i < g_gt_tnum; i++) {
            if (tp_dev.sta & (1 << i)) {
                gt9xxx_rd_reg(GT9XXX_TPX_TBL[i], buf, 4);

                if (lcddev.id == 0x9881d || lcddev.id == 0x7703 || lcddev.id == 0x9881c8 ||
                    lcddev.id == 0x79007 || lcddev.id == 0x9881c10 || lcddev.id == 0x7796) {
                    tp_dev.x[i] = ((uint16_t)buf[1] << 8) + buf[0];
                    tp_dev.y[i] = ((uint16_t)buf[3] << 8) + buf[2];
                    /* MADCTL 硬件横屏时在此映射；PPA 逻辑横屏改在 lvgl touchpad_get_xy */
                    if (lcddev.id == 0x7796 && lcddev.dir == 1) {
                        int32_t rx = tp_dev.x[i];
                        int32_t ry = tp_dev.y[i];
                        tp_dev.x[i] = ry;
                        tp_dev.y[i] = (int32_t)lcddev.height - 1 - rx;
                    }
                } else {
                    tp_dev.x[i] = lcddev.width - (((uint16_t)buf[1] << 8) + buf[0]);
                    tp_dev.y[i] = lcddev.height - (((uint16_t)buf[3] << 8) + buf[2]);
                }
            }
        }

        res = 1;

        if (tp_dev.x[0] > lcddev.width || tp_dev.y[0] > lcddev.height) {
            if ((mode & 0XF) > 1) {
                tp_dev.x[0] = tp_dev.x[1];
                tp_dev.y[0] = tp_dev.y[1];
            } else {
                tp_dev.x[0] = tp_dev.x[g_gt_tnum - 1];
                tp_dev.y[0] = tp_dev.y[g_gt_tnum - 1];
                mode = 0X80;
                tp_dev.sta = tempsta;
            }
        }
    }

    if ((mode & 0X8F) == 0X80) {
        if (tp_dev.sta & TP_PRES_DOWN) {
            tp_dev.sta &= ~TP_PRES_DOWN;
        } else {
            tp_dev.x[0] = 0xffff;
            tp_dev.y[0] = 0xffff;
            tp_dev.sta &= 0XE000;
        }
    }

    return res;
}
