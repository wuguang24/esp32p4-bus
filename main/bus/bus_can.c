/**
 * @file bus_can.c
 * @brief TWAI 新驱动 esp_twai / esp_twai_onchip
 */

#include "bus_can.h"

#include <string.h>

#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bus_capture.h"
#include "bus_pins.h"

static const char *TAG = "bus_can";

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t flags; /* BUS_FLAG_* */
    uint8_t data[8];
    uint8_t err;
} can_rx_item_t;

static twai_node_handle_t s_node;
static bool s_running;
static bool s_listen_only = true;
static bool s_allow_tx;
static uint32_t s_baud = BUS_CAN_BAUD_DEFAULT;
static bool s_filter_en;
static bool s_filter_ext;
static uint32_t s_filter_id;
static uint32_t s_filter_mask = 0x7FFu;

static QueueHandle_t s_rxq;
static TaskHandle_t s_rx_task;
static volatile bool s_rx_stop;

static bus_can_frame_cb_t s_rx_cb;
static void *s_rx_cb_user;

static uint32_t s_bus_err_cnt;
static uint32_t s_err_arb, s_err_bit, s_err_form, s_err_stuff, s_err_ack;
static char s_last_err[40];

static void set_last_err(const char *s)
{
    if (!s) {
        return;
    }
    strncpy(s_last_err, s, sizeof(s_last_err) - 1);
    s_last_err[sizeof(s_last_err) - 1] = '\0';
}

static void frame_from_item(const can_rx_item_t *it, bus_frame_t *f, uint8_t dir)
{
    memset(f, 0, sizeof(*f));
    bus_frame_stamp(f);
    f->src = BUS_SRC_CAN;
    f->dir = dir;
    f->id = it->id;
    f->dlc = it->dlc;
    f->flags = it->flags;
    if (it->err) {
        f->flags |= BUS_FLAG_ERR;
        f->len = 0;
        return;
    }
    if (f->flags & BUS_FLAG_RTR) {
        f->len = 0;
    } else {
        f->len = (it->dlc > 8) ? 8 : it->dlc;
        if (f->len) {
            memcpy(f->data, it->data, f->len);
        }
    }
}

static IRAM_ATTR bool on_rx_done(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx)
{
    (void)edata;
    (void)user_ctx;
    uint8_t buf[8];
    twai_frame_t rx = {
        .buffer = buf,
        .buffer_len = sizeof(buf),
    };
    if (twai_node_receive_from_isr(handle, &rx) != ESP_OK) {
        return false;
    }
    can_rx_item_t it = {0};
    it.id = rx.header.id;
    it.dlc = (uint8_t)((rx.header.dlc > 8) ? 8 : rx.header.dlc);
    if (rx.header.ide) {
        it.flags |= BUS_FLAG_EXT;
    }
    if (rx.header.rtr) {
        it.flags |= BUS_FLAG_RTR;
    } else if (it.dlc) {
        memcpy(it.data, buf, it.dlc);
    }
    BaseType_t hp = pdFALSE;
    if (s_rxq) {
        xQueueSendFromISR(s_rxq, &it, &hp);
    }
    return hp == pdTRUE;
}

static IRAM_ATTR bool on_error(twai_node_handle_t handle, const twai_error_event_data_t *edata, void *user_ctx)
{
    (void)handle;
    (void)user_ctx;
    s_bus_err_cnt++;
    uint32_t flags_val = 0;
    if (edata) {
        twai_error_flags_t f = edata->err_flags;
        flags_val = f.val;
        if (f.arb_lost) {
            s_err_arb++;
        }
        if (f.bit_err) {
            s_err_bit++;
        }
        if (f.form_err) {
            s_err_form++;
        }
        if (f.stuff_err) {
            s_err_stuff++;
        }
        if (f.ack_err) {
            s_err_ack++;
        }
    }
    if (s_rxq) {
        can_rx_item_t it = {.err = 1, .flags = BUS_FLAG_ERR, .id = flags_val};
        BaseType_t hp = pdFALSE;
        xQueueSendFromISR(s_rxq, &it, &hp);
        return hp == pdTRUE;
    }
    return false;
}

static void rx_task(void *arg)
{
    (void)arg;
    can_rx_item_t it;
    bus_frame_t frame;
    while (!s_rx_stop) {
        if (xQueueReceive(s_rxq, &it, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        if (it.err) {
            twai_error_flags_t f = {.val = it.id};
            if (f.ack_err) {
                set_last_err("ACK错误");
            } else if (f.stuff_err) {
                set_last_err("填充错误");
            } else if (f.form_err) {
                set_last_err("格式错误");
            } else if (f.bit_err) {
                set_last_err("位错误");
            } else if (f.arb_lost) {
                set_last_err("仲裁丢失");
            } else {
                set_last_err("总线错误");
            }
        }
        frame_from_item(&it, &frame, BUS_DIR_RX);
        (void)bus_capture_push(&frame);
        if (s_rx_cb) {
            s_rx_cb(&frame, s_rx_cb_user);
        }
    }
    s_rx_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t apply_filter_unlocked(void)
{
    if (!s_node) {
        return ESP_ERR_INVALID_STATE;
    }
    twai_mask_filter_config_t cfg = {0};
    if (!s_filter_en) {
        cfg.id = 0;
        cfg.mask = 0;
        cfg.is_ext = false;
    } else {
        cfg.id = s_filter_id;
        cfg.mask = s_filter_mask ? s_filter_mask : (s_filter_ext ? 0x1FFFFFFFu : 0x7FFu);
        cfg.is_ext = s_filter_ext;
    }
    return twai_node_config_mask_filter(s_node, 0, &cfg);
}

esp_err_t bus_can_start(uint32_t baud, bool listen_only)
{
    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (baud < BUS_CAN_BAUD_MIN || baud > BUS_CAN_BAUD_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_rxq) {
        s_rxq = xQueueCreate(64, sizeof(can_rx_item_t));
        if (!s_rxq) {
            return ESP_ERR_NO_MEM;
        }
    }

    twai_onchip_node_config_t cfg = {
        .io_cfg = {
            .tx = BUS_CAN_TX_GPIO,
            .rx = BUS_CAN_RX_GPIO,
            .quanta_clk_out = GPIO_NUM_NC,
            .bus_off_indicator = GPIO_NUM_NC,
        },
        .bit_timing = {
            .bitrate = baud,
        },
        .tx_queue_depth = 16,
        .flags = {
            .enable_listen_only = listen_only ? 1u : 0u,
        },
    };

    esp_err_t err = twai_new_node_onchip(&cfg, &s_node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_new_node_onchip: %s", esp_err_to_name(err));
        return err;
    }

    twai_event_callbacks_t cbs = {
        .on_rx_done = on_rx_done,
        .on_error = on_error,
    };
    err = twai_node_register_event_callbacks(s_node, &cbs, NULL);
    if (err != ESP_OK) {
        twai_node_delete(s_node);
        s_node = NULL;
        return err;
    }

    err = apply_filter_unlocked();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "filter config: %s", esp_err_to_name(err));
    }

    err = twai_node_enable(s_node);
    if (err != ESP_OK) {
        twai_node_delete(s_node);
        s_node = NULL;
        return err;
    }

    s_baud = baud;
    s_listen_only = listen_only;
    s_rx_stop = false;
    s_bus_err_cnt = 0;
    s_err_arb = s_err_bit = s_err_form = s_err_stuff = s_err_ack = 0;
    s_last_err[0] = '\0';
    s_running = true;

    BaseType_t ok = xTaskCreate(rx_task, "bus_can_rx", 3072, NULL, 5, &s_rx_task);
    if (ok != pdPASS) {
        s_running = false;
        twai_node_disable(s_node);
        twai_node_delete(s_node);
        s_node = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "esp_twai start %lu listen=%d filt=%d",
             (unsigned long)baud, (int)listen_only, (int)s_filter_en);
    return ESP_OK;
}

esp_err_t bus_can_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    s_rx_stop = true;
    for (int i = 0; i < 50 && s_rx_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_node) {
        (void)twai_node_disable(s_node);
        (void)twai_node_delete(s_node);
        s_node = NULL;
    }
    s_running = false;
    ESP_LOGI(TAG, "stopped");
    return ESP_OK;
}

bool bus_can_is_running(void)
{
    return s_running;
}

uint32_t bus_can_get_baud(void)
{
    return s_baud;
}

bool bus_can_get_listen_only(void)
{
    return s_listen_only;
}

void bus_can_set_allow_tx(bool allow)
{
    s_allow_tx = allow;
}

bool bus_can_get_allow_tx(void)
{
    return s_allow_tx;
}

esp_err_t bus_can_set_filter(bool enable, uint32_t id, uint32_t mask, bool ext)
{
    s_filter_en = enable;
    s_filter_id = id;
    s_filter_mask = mask;
    s_filter_ext = ext;
    if (!s_running || !s_node) {
        return ESP_OK;
    }
    esp_err_t err = twai_node_disable(s_node);
    if (err != ESP_OK) {
        return err;
    }
    err = apply_filter_unlocked();
    esp_err_t e2 = twai_node_enable(s_node);
    return (err != ESP_OK) ? err : e2;
}

esp_err_t bus_can_send(uint32_t id, bool ext, const uint8_t *data, size_t len)
{
    if (!s_running || !s_node) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_allow_tx || s_listen_only) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > 8 || (len > 0 && data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[8] = {0};
    if (len) {
        memcpy(buf, data, len);
    }
    twai_frame_t tx = {
        .header = {
            .id = id,
            .dlc = (uint16_t)len,
            .ide = ext ? 1u : 0u,
        },
        .buffer = buf,
        .buffer_len = len,
    };
    esp_err_t err = twai_node_transmit(s_node, &tx, 100);
    if (err == ESP_OK) {
        can_rx_item_t it = {
            .id = id,
            .dlc = (uint8_t)len,
            .flags = ext ? BUS_FLAG_EXT : 0,
        };
        memcpy(it.data, buf, len);
        bus_frame_t frame;
        frame_from_item(&it, &frame, BUS_DIR_TX);
        (void)bus_capture_push(&frame);
    }
    return err;
}

void bus_can_get_status(bus_can_status_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->running = s_running;
    out->listen_only = s_listen_only;
    out->baud = s_baud;
    out->filter_en = s_filter_en;
    out->filter_ext = s_filter_ext;
    out->filter_id = s_filter_id;
    out->filter_mask = s_filter_mask;
    out->bus_err = s_bus_err_cnt;
    out->err_arb = s_err_arb;
    out->err_bit = s_err_bit;
    out->err_form = s_err_form;
    out->err_stuff = s_err_stuff;
    out->err_ack = s_err_ack;
    memcpy(out->last_err, s_last_err, sizeof(out->last_err));
    if (s_node) {
        twai_node_status_t st = {0};
        twai_node_record_t rec = {0};
        if (twai_node_get_info(s_node, &st, &rec) == ESP_OK) {
            out->tec = st.tx_error_count;
            out->rec = st.rx_error_count;
            out->state = (uint32_t)st.state;
            out->bus_err = rec.bus_err_num;
        }
    }
}

void bus_can_set_rx_cb(bus_can_frame_cb_t cb, void *user)
{
    s_rx_cb = cb;
    s_rx_cb_user = user;
}
