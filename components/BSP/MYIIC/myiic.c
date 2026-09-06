/**
 ******************************************************************************
 * @file        iic.c
 * @version     V1.0
 * @brief       IIC驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

#include "myiic.h"


i2c_master_bus_handle_t touch_i2c_bus;     /* 触摸 I2C0 总线句柄 */

/**
 * @brief       初始化MYIIC
 * @param       无
 * @retval      ESP_OK:初始化成功
 */
esp_err_t myiic_init(void)
{
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source                     = I2C_CLK_SRC_DEFAULT,  /* 时钟源 */
        .i2c_port                       = IIC_NUM_PORT,         /* I2C端口 */
        .scl_io_num                     = IIC_SCL_GPIO_PIN,     /* SCL管脚 */
        .sda_io_num                     = IIC_SDA_GPIO_PIN,     /* SDA管脚 */
        .glitch_ignore_cnt              = 7,                    /* 故障周期 */
        .flags.enable_internal_pullup   = true,                 /* 内部上拉 */
    };
    /* 新建I2C总线 */
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &touch_i2c_bus));

    return ESP_OK;
}
