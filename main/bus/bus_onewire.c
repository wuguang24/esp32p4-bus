/**
 * @file bus_onewire.c
 * @brief 标准时序 1-Wire（开漏 bitbang）
 */

#include "bus_onewire.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef BUS_DIO_GPIO_DEFAULT
#define BUS_DIO_GPIO_DEFAULT GPIO_NUM_35
#endif

static gpio_num_t s_gpio = BUS_DIO_GPIO_DEFAULT;

static void ow_release(void)
{
    gpio_set_direction(s_gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(s_gpio, GPIO_PULLUP_ONLY);
}

static void ow_drive_low(void)
{
    gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(s_gpio, 0);
}

static int ow_sample(void)
{
    return gpio_get_level(s_gpio);
}

esp_err_t bus_ow_init(gpio_num_t gpio)
{
    if (gpio >= 0) {
        s_gpio = gpio;
    }
    gpio_reset_pin(s_gpio);
    ow_release();
    return ESP_OK;
}

gpio_num_t bus_ow_get_gpio(void)
{
    return s_gpio;
}

bool bus_ow_reset(void)
{
    portDISABLE_INTERRUPTS();
    ow_drive_low();
    esp_rom_delay_us(480);
    ow_release();
    esp_rom_delay_us(70);
    int presence = !ow_sample();
    esp_rom_delay_us(410);
    portENABLE_INTERRUPTS();
    return presence != 0;
}

static void ow_write_bit(int bit)
{
    portDISABLE_INTERRUPTS();
    if (bit) {
        ow_drive_low();
        esp_rom_delay_us(6);
        ow_release();
        esp_rom_delay_us(64);
    } else {
        ow_drive_low();
        esp_rom_delay_us(60);
        ow_release();
        esp_rom_delay_us(10);
    }
    portENABLE_INTERRUPTS();
}

static int ow_read_bit(void)
{
    portDISABLE_INTERRUPTS();
    ow_drive_low();
    esp_rom_delay_us(6);
    ow_release();
    esp_rom_delay_us(9);
    int b = ow_sample();
    esp_rom_delay_us(55);
    portENABLE_INTERRUPTS();
    return b;
}

esp_err_t bus_ow_write_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(b & 1);
        b >>= 1;
    }
    return ESP_OK;
}

uint8_t bus_ow_read_byte(void)
{
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) {
        if (ow_read_bit()) {
            v |= (uint8_t)(1u << i);
        }
    }
    return v;
}

esp_err_t bus_ow_read_rom(uint8_t rom[8])
{
    if (!rom) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bus_ow_reset()) {
        return ESP_ERR_NOT_FOUND;
    }
    (void)bus_ow_write_byte(0x33);
    for (int i = 0; i < 8; i++) {
        rom[i] = bus_ow_read_byte();
    }
    return ESP_OK;
}

/* 简化：仅支持总线上 1 个设备时用 Read ROM；多设备只返回第一个 */
int bus_ow_search(uint8_t roms[][8], int max_n)
{
    if (!roms || max_n <= 0) {
        return 0;
    }
    if (bus_ow_read_rom(roms[0]) != ESP_OK) {
        return 0;
    }
    return 1;
}
