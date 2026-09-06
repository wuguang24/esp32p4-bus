/**
 * @file bus_i2c.c
 * @brief I2C1 主机（SDA7 SCL8）扫描/读写，结果入 capture
 */

#include "bus_i2c.h"

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bus_capture.h"
#include "bus_pins.h"

static const char *TAG = "bus_i2c";

static bool s_running;
static uint32_t s_freq = BUS_I2C_FREQ_DEFAULT_HZ;
static i2c_master_bus_handle_t s_bus;

static void push_xfer(uint8_t addr, uint8_t dir, const uint8_t *data, size_t len, bool ok)
{
    bus_frame_t f = {0};
    bus_frame_stamp(&f);
    f.src = BUS_SRC_I2C;
    f.dir = dir;
    f.id = addr;
    f.flags = ok ? 0 : BUS_FLAG_ERR;
    if (!ok) {
        f.flags |= BUS_FLAG_NACK;
    }
    f.len = (uint8_t)((len > 64) ? 64 : len);
    f.dlc = f.len;
    if (data && f.len) {
        memcpy(f.data, data, f.len);
    }
    (void)bus_capture_push(&f);
}

esp_err_t bus_i2c_start(uint32_t freq_hz)
{
    if (s_running) {
        (void)bus_i2c_stop();
    }
    if (freq_hz == 0) {
        freq_hz = BUS_I2C_FREQ_DEFAULT_HZ;
    }
    s_freq = freq_hz;

    i2c_master_bus_config_t cfg = {
        .i2c_port = BUS_I2C_PORT,
        .sda_io_num = BUS_I2C_SDA_GPIO,
        .scl_io_num = BUS_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
        return err;
    }
    s_running = true;
    ESP_LOGD(TAG, "I2C1 start %lu Hz SDA%d SCL%d", (unsigned long)freq_hz,
             (int)BUS_I2C_SDA_GPIO, (int)BUS_I2C_SCL_GPIO);
    return ESP_OK;
}

esp_err_t bus_i2c_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    if (s_bus) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    s_running = false;
    return ESP_OK;
}

bool bus_i2c_is_running(void)
{
    return s_running;
}

uint32_t bus_i2c_get_freq(void)
{
    return s_freq;
}

int bus_i2c_scan(uint8_t *found, int max_n)
{
    if (!s_running || found == NULL || max_n <= 0) {
        return 0;
    }
    int n = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        esp_err_t e = i2c_master_probe(s_bus, addr, 50);
        if (e == ESP_OK) {
            found[n++] = addr;
            push_xfer(addr, BUS_DIR_RX, NULL, 0, true);
            if (n >= max_n) {
                break;
            }
        }
    }
    ESP_LOGI(TAG, "scan found %d", n);
    return n;
}

static esp_err_t with_dev(uint8_t addr7, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr7,
        .scl_speed_hz = s_freq,
    };
    return i2c_master_bus_add_device(s_bus, &dev, out);
}

esp_err_t bus_i2c_write(uint8_t addr7, const uint8_t *data, size_t len)
{
    if (!s_running || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    i2c_master_dev_handle_t h = NULL;
    esp_err_t err = with_dev(addr7, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit(h, data, len, 200);
    i2c_master_bus_rm_device(h);
    push_xfer(addr7, BUS_DIR_TX, data, len, err == ESP_OK);
    return err;
}

esp_err_t bus_i2c_read(uint8_t addr7, uint8_t *data, size_t len)
{
    if (!s_running || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    i2c_master_dev_handle_t h = NULL;
    esp_err_t err = with_dev(addr7, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_receive(h, data, len, 200);
    i2c_master_bus_rm_device(h);
    push_xfer(addr7, BUS_DIR_RX, data, len, err == ESP_OK);
    return err;
}

esp_err_t bus_i2c_write_read(uint8_t addr7, const uint8_t *wr, size_t wr_len,
                             uint8_t *rd, size_t rd_len)
{
    if (!s_running || rd == NULL || rd_len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    i2c_master_dev_handle_t h = NULL;
    esp_err_t err = with_dev(addr7, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit_receive(h, wr, wr_len, rd, rd_len, 200);
    i2c_master_bus_rm_device(h);
    if (wr && wr_len) {
        push_xfer(addr7, BUS_DIR_TX, wr, wr_len, err == ESP_OK);
    }
    push_xfer(addr7, BUS_DIR_RX, rd, rd_len, err == ESP_OK);
    return err;
}
