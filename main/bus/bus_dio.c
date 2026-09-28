/**
 * @file bus_dio.c
 */

#include "bus_dio.h"

#include "esp_log.h"

static const char *TAG = "bus_dio";
static gpio_num_t s_gpio = BUS_DIO_GPIO_DEFAULT;
static bus_dio_mode_t s_mode = BUS_DIO_IN;
static bool s_ready;

esp_err_t bus_dio_init(gpio_num_t gpio)
{
    if (gpio < 0) {
        gpio = BUS_DIO_GPIO_DEFAULT;
    }
    s_gpio = gpio;
    s_ready = true;
    return bus_dio_set_mode(BUS_DIO_IN);
}

gpio_num_t bus_dio_get_gpio(void)
{
    return s_gpio;
}

esp_err_t bus_dio_set_mode(bus_dio_mode_t mode)
{
    if (!s_ready) {
        (void)bus_dio_init(s_gpio);
    }
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << s_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    switch (mode) {
    case BUS_DIO_OUT:
        io.mode = GPIO_MODE_INPUT_OUTPUT;
        break;
    case BUS_DIO_IN_PU:
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        break;
    case BUS_DIO_IN_PD:
        io.pull_down_en = GPIO_PULLDOWN_ENABLE;
        break;
    default:
        break;
    }
    esp_err_t err = gpio_config(&io);
    if (err == ESP_OK) {
        s_mode = mode;
        ESP_LOGD(TAG, "GPIO%d mode=%d", (int)s_gpio, (int)mode);
    }
    return err;
}

bus_dio_mode_t bus_dio_get_mode(void)
{
    return s_mode;
}

esp_err_t bus_dio_write(bool level)
{
    if (s_mode != BUS_DIO_OUT) {
        esp_err_t e = bus_dio_set_mode(BUS_DIO_OUT);
        if (e != ESP_OK) {
            return e;
        }
    }
    return gpio_set_level(s_gpio, level ? 1 : 0);
}

int bus_dio_read(void)
{
    if (!s_ready) {
        (void)bus_dio_init(s_gpio);
    }
    return gpio_get_level(s_gpio);
}
