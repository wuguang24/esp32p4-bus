/**
 * @file bus_nvs.c
 */

#include "bus_nvs.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "bus_nvs";
static const char *NS = "bus_cfg";
static const char *KEY = "blob";

#define BUS_NVS_MAGIC  0x42555331u /* 'BUS1' */
#define BUS_NVS_VER    1u

typedef struct __attribute__((packed)) {
    uint8_t mode;
    uint8_t fixed_len;
    uint8_t sof[4];
    uint8_t sof_n;
    uint8_t eof[4];
    uint8_t eof_n;
} bus_nvs_fr_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t can_baud;
    uint32_t rs485_baud;
    uint32_t uart_baud;
    uint32_t i2c_hz;
    uint32_t spi_hz;
    uint32_t can_filt_id;
    uint32_t can_filt_mask;
    uint8_t spi_mode;
    uint8_t rs485_parity;
    uint8_t uart_parity;
    uint8_t uart_stop;
    uint8_t listen_only;
    uint8_t allow_tx;
    uint8_t can_filt_en;
    uint8_t can_filt_ext;
    bus_nvs_fr_t uart_fr;
    bus_nvs_fr_t rs485_fr;
} bus_nvs_blob_t;

static void pack_fr(bus_nvs_fr_t *d, const bus_framer_cfg_t *s)
{
    memset(d, 0, sizeof(*d));
    if (!s) {
        return;
    }
    d->mode = (uint8_t)s->mode;
    d->fixed_len = s->fixed_len;
    memcpy(d->sof, s->sof, sizeof(d->sof));
    d->sof_n = s->sof_n > 4 ? 4 : s->sof_n;
    memcpy(d->eof, s->eof, sizeof(d->eof));
    d->eof_n = s->eof_n > 4 ? 4 : s->eof_n;
}

static void unpack_fr(bus_framer_cfg_t *d, const bus_nvs_fr_t *s)
{
    memset(d, 0, sizeof(*d));
    if (!s) {
        return;
    }
    d->mode = (bus_framer_mode_t)s->mode;
    if (d->mode > BUS_FR_MARK) {
        d->mode = BUS_FR_IDLE;
    }
    d->fixed_len = s->fixed_len ? s->fixed_len : 8;
    memcpy(d->sof, s->sof, sizeof(d->sof));
    d->sof_n = s->sof_n > 4 ? 4 : s->sof_n;
    memcpy(d->eof, s->eof, sizeof(d->eof));
    d->eof_n = s->eof_n > 4 ? 4 : s->eof_n;
}

void bus_nvs_defaults(bus_nvs_cfg_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->can_baud = BUS_CAN_BAUD_DEFAULT;
    out->rs485_baud = BUS_RS485_BAUD_DEFAULT;
    out->uart_baud = BUS_UART_BAUD_DEFAULT;
    out->i2c_hz = BUS_I2C_FREQ_DEFAULT_HZ;
    out->spi_hz = BUS_SPI_FREQ_DEFAULT_HZ;
    out->spi_mode = 0;
    out->rs485_parity = BUS_RS485_PARITY_NONE;
    out->uart_parity = BUS_UART_PARITY_NONE;
    out->uart_stop = BUS_UART_STOP_1;
    out->listen_only = true;
    out->allow_tx = false;
    out->can_filt_en = false;
    out->can_filt_ext = false;
    out->can_filt_id = 0;
    out->can_filt_mask = 0x7FFu;
    out->uart_fr.mode = BUS_FR_IDLE;
    out->uart_fr.fixed_len = 8;
    out->rs485_fr.mode = BUS_FR_IDLE;
    out->rs485_fr.fixed_len = 8;
}

bool bus_nvs_load(bus_nvs_cfg_t *out)
{
    if (!out) {
        return false;
    }
    bus_nvs_defaults(out);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bus_nvs_blob_t blob;
    size_t len = sizeof(blob);
    esp_err_t err = nvs_get_blob(h, KEY, &blob, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(blob)) {
        return false;
    }
    if (blob.magic != BUS_NVS_MAGIC || blob.version != BUS_NVS_VER) {
        return false;
    }

    out->can_baud = blob.can_baud;
    out->rs485_baud = blob.rs485_baud;
    out->uart_baud = blob.uart_baud;
    out->i2c_hz = blob.i2c_hz;
    out->spi_hz = blob.spi_hz;
    out->spi_mode = blob.spi_mode > 3 ? 0 : blob.spi_mode;
    out->rs485_parity = (bus_rs485_parity_t)(blob.rs485_parity % 3);
    out->uart_parity = (bus_uart_parity_t)(blob.uart_parity % 3);
    out->uart_stop = blob.uart_stop ? BUS_UART_STOP_2 : BUS_UART_STOP_1;
    out->listen_only = blob.listen_only != 0;
    out->allow_tx = blob.allow_tx != 0;
    out->can_filt_en = blob.can_filt_en != 0;
    out->can_filt_ext = blob.can_filt_ext != 0;
    out->can_filt_id = blob.can_filt_id;
    out->can_filt_mask = blob.can_filt_mask ? blob.can_filt_mask : 0x7FFu;
    unpack_fr(&out->uart_fr, &blob.uart_fr);
    unpack_fr(&out->rs485_fr, &blob.rs485_fr);

    if (out->can_baud < BUS_CAN_BAUD_MIN || out->can_baud > BUS_CAN_BAUD_MAX) {
        out->can_baud = BUS_CAN_BAUD_DEFAULT;
    }
    if (out->rs485_baud < BUS_RS485_BAUD_MIN || out->rs485_baud > BUS_RS485_BAUD_MAX) {
        out->rs485_baud = BUS_RS485_BAUD_DEFAULT;
    }
    if (out->uart_baud == 0 || out->uart_baud > BUS_UART_BAUD_MAX) {
        out->uart_baud = BUS_UART_BAUD_DEFAULT;
    }
    ESP_LOGI(TAG, "loaded CAN=%lu RS485=%lu UART=%lu",
             (unsigned long)out->can_baud, (unsigned long)out->rs485_baud,
             (unsigned long)out->uart_baud);
    return true;
}

bool bus_nvs_save(const bus_nvs_cfg_t *in)
{
    if (!in) {
        return false;
    }
    bus_nvs_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = BUS_NVS_MAGIC;
    blob.version = BUS_NVS_VER;
    blob.can_baud = in->can_baud;
    blob.rs485_baud = in->rs485_baud;
    blob.uart_baud = in->uart_baud;
    blob.i2c_hz = in->i2c_hz;
    blob.spi_hz = in->spi_hz;
    blob.spi_mode = in->spi_mode;
    blob.rs485_parity = (uint8_t)in->rs485_parity;
    blob.uart_parity = (uint8_t)in->uart_parity;
    blob.uart_stop = (in->uart_stop == BUS_UART_STOP_2) ? 1u : 0u;
    blob.listen_only = in->listen_only ? 1u : 0u;
    blob.allow_tx = in->allow_tx ? 1u : 0u;
    blob.can_filt_en = in->can_filt_en ? 1u : 0u;
    blob.can_filt_ext = in->can_filt_ext ? 1u : 0u;
    blob.can_filt_id = in->can_filt_id;
    blob.can_filt_mask = in->can_filt_mask;
    pack_fr(&blob.uart_fr, &in->uart_fr);
    pack_fr(&blob.rs485_fr, &in->rs485_fr);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(h, KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save fail: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "saved");
    return true;
}

bool bus_nvs_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    (void)nvs_erase_key(h, KEY);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}
