/**
 * @file main.c
 * @brief ESP32-P4 总线通信分析仪：CAN/RS485/UART/I2C/SPI + LVGL + WiFi
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "lvgl_demo.h"
#include "wifi_bringup.h"
#include "bus_app.h"
#include "bus_stream.h"
#include "bus_proto.h"
#include "siggen_pwm.h"
#include "bus_adc.h"
#include "bus_web.h"
#include "bus_cli_io.h"
#include "bus_dio.h"
#include "bus_onewire.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    if (wifi_bringup_start() == ESP_OK) {
        ESP_LOGI(TAG, "WiFi APSTA started");
    } else {
        ESP_LOGW(TAG, "WiFi unavailable");
    }

    lvgl_demo();

    if (bus_app_start() != ESP_OK) {
        ESP_LOGE(TAG, "bus_app_start failed");
    } else {
        ESP_LOGI(TAG, "bus analyzer started");
    }

    if (siggen_pwm_init() == ESP_OK) {
        ESP_LOGI(TAG, "PWM GPIO%d ready", (int)siggen_pwm_get_gpio());
    }
    if (bus_adc_init() == ESP_OK) {
        ESP_LOGI(TAG, "ADC GPIO%d ready", (int)bus_adc_get_gpio());
    }
    if (bus_dio_init(BUS_DIO_GPIO_DEFAULT) == ESP_OK) {
        ESP_LOGI(TAG, "DIO GPIO%d ready", (int)bus_dio_get_gpio());
    }
    (void)bus_ow_init(bus_dio_get_gpio());

    if (bus_stream_start() == ESP_OK) {
        ESP_LOGI(TAG, "BUS1 UDP :%d", BUS1_UDP_PORT);
    }

    if (bus_web_start() == ESP_OK) {
        ESP_LOGI(TAG, "Web CLI http://192.168.4.1/");
    }
    if (bus_cli_io_start() == ESP_OK) {
        ESP_LOGI(TAG, "CLI IO USB/TCP:2323");
    }
}
