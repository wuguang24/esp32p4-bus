/**
 * @file bus_spi.c
 * @brief SPI 主机（SCLK26 MOSI27 MISO6 CS12）
 *
 * Hosted SDIO 已占大量 INTERNAL|DMA，SPI_DMA_CH_AUTO 常 ESP_ERR_NO_MEM。
 * 本分析仪单次 ≤64B，优先无 DMA；失败再试 DMA / 另一 GPSPI。
 */

#include "bus_spi.h"

#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "soc/soc_caps.h"
#include "bus_capture.h"
#include "bus_frame.h"
#include "bus_pins.h"

static const char *TAG = "bus_spi";

static bool s_running;
static uint32_t s_freq = BUS_SPI_FREQ_DEFAULT_HZ;
static uint8_t s_mode;
static spi_device_handle_t s_dev;
static spi_host_device_t s_host = BUS_SPI_HOST;

static esp_err_t bus_init_on_host(spi_host_device_t host, uint32_t freq_hz, uint8_t mode)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = BUS_SPI_MOSI_GPIO,
        .miso_io_num = BUS_SPI_MISO_GPIO,
        .sclk_io_num = BUS_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };

    /* 先无 DMA（省内部 DMA 堆），再 DMA */
    esp_err_t err = spi_bus_initialize(host, &buscfg, SPI_DMA_DISABLED);
    if (err == ESP_ERR_INVALID_STATE) {
        /* 总线已在：尝试直接挂设备 */
    } else if (err == ESP_ERR_NO_MEM) {
        ESP_LOGW(TAG, "SPI%u no-DMA NO_MEM, try DMA", (unsigned)host);
        err = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
    } else if (err != ESP_OK) {
        return err;
    }

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = (int)freq_hz,
        .mode = mode,
        .spics_io_num = BUS_SPI_CS_GPIO,
        .queue_size = 2,
    };
    err = spi_bus_add_device(host, &devcfg, &s_dev);
    if (err != ESP_OK) {
        if (err != ESP_ERR_INVALID_STATE) {
            spi_bus_free(host);
        }
        s_dev = NULL;
        return err;
    }
    s_host = host;
    return ESP_OK;
}

esp_err_t bus_spi_start(uint32_t freq_hz, uint8_t mode)
{
    if (s_running) {
        (void)bus_spi_stop();
    }
    if (freq_hz == 0) {
        freq_hz = BUS_SPI_FREQ_DEFAULT_HZ;
    }
    if (mode > 3) {
        mode = 0;
    }
    s_freq = freq_hz;
    s_mode = mode;

    esp_err_t err = bus_init_on_host(BUS_SPI_HOST, freq_hz, mode);
#if SOC_SPI_PERIPH_NUM > 2
    if (err != ESP_OK && BUS_SPI_HOST != SPI3_HOST) {
        ESP_LOGW(TAG, "SPI host %d fail (%s), try SPI3", (int)BUS_SPI_HOST, esp_err_to_name(err));
        err = bus_init_on_host(SPI3_HOST, freq_hz, mode);
    }
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi start: %s", esp_err_to_name(err));
        return err;
    }
    s_running = true;
    ESP_LOGI(TAG, "SPI start %lu Hz mode%u host=%d",
             (unsigned long)freq_hz, (unsigned)mode, (int)s_host);
    return ESP_OK;
}

esp_err_t bus_spi_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    if (s_dev) {
        spi_bus_remove_device(s_dev);
        s_dev = NULL;
    }
    spi_bus_free(s_host);
    s_running = false;
    return ESP_OK;
}

bool bus_spi_is_running(void)
{
    return s_running;
}

uint32_t bus_spi_get_freq(void)
{
    return s_freq;
}

uint8_t bus_spi_get_mode(void)
{
    return s_mode;
}

esp_err_t bus_spi_xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (!s_running || !s_dev || len == 0 || len > 64) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t txbuf[64] = {0};
    uint8_t rxbuf[64] = {0};
    if (tx) {
        memcpy(txbuf, tx, len);
    }
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = txbuf,
        .rx_buffer = rxbuf,
    };
    esp_err_t err = spi_device_transmit(s_dev, &t);

    bus_frame_t f = {0};
    bus_frame_stamp(&f);
    f.src = BUS_SRC_SPI;
    f.dir = BUS_DIR_TX;
    f.flags = (err == ESP_OK) ? 0 : BUS_FLAG_ERR;
    f.len = (uint8_t)len;
    f.dlc = f.len;
    memcpy(f.data, txbuf, len);
    (void)bus_capture_push(&f);
    if (err == ESP_OK) {
        if (rx) {
            memcpy(rx, rxbuf, len);
        }
        bus_frame_t fr = f;
        fr.dir = BUS_DIR_RX;
        fr.flags = 0;
        memcpy(fr.data, rxbuf, len);
        (void)bus_capture_push(&fr);
    }
    return err;
}
