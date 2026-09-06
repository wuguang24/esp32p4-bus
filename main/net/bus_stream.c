/**
 * @file bus_stream.c
 * @brief BUS1 UDP 流：遥测 + 帧转发（WiFi SoftAP/STA）
 */

#include "bus_stream.h"
#include "bus_proto.h"
#include "bus_app.h"
#include "bus_capture.h"
#include "bus_can.h"
#include "bus_rs485.h"
#include "bus_uart.h"
#include "bus_i2c.h"
#include "wifi_bringup.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <unistd.h>

static const char *TAG = "bus_stream";

static int s_sock = -1;
static struct sockaddr_in s_peer;
static bool s_peer_ok;
static TaskHandle_t s_task;
static uint16_t s_seq;
static uint64_t s_frame_seq;

static void send_pkt(uint8_t type, const void *payload, uint16_t plen)
{
    if (s_sock < 0 || !s_peer_ok) {
        return;
    }
    uint8_t buf[sizeof(bus1_hdr_t) + 128];
    if (plen > 128) {
        plen = 128;
    }
    bus1_hdr_t *h = (bus1_hdr_t *)buf;
    h->magic = BUS1_MAGIC;
    h->type = type;
    h->flags = 0;
    h->seq = s_seq++;
    h->tick_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    h->len = plen;
    if (payload && plen) {
        memcpy(buf + sizeof(bus1_hdr_t), payload, plen);
    }
    sendto(s_sock, buf, sizeof(bus1_hdr_t) + plen, 0,
           (struct sockaddr *)&s_peer, sizeof(s_peer));
}

static void handle_cmd(const bus1_cmd_t *c)
{
    bus1_ack_t ack = {.cmd_id = c->cmd_id, .result = 0};
    esp_err_t err = ESP_OK;
    switch (c->cmd_id) {
    case BUS1_CMD_CLEAR:
        bus_app_clear_capture();
        break;
    case BUS1_CMD_SET_ALLOW_TX:
        err = bus_app_set_allow_tx(c->b0 != 0);
        break;
    case BUS1_CMD_SET_CAN_BAUD:
        err = bus_app_set_can_baud(c->u0);
        break;
    case BUS1_CMD_SET_RS485_BAUD:
        err = bus_app_set_rs485_baud(c->u0);
        break;
    case BUS1_CMD_SET_UART_BAUD:
        err = bus_app_set_uart_baud(c->u0);
        break;
    case BUS1_CMD_LOGGER_START:
        err = bus_app_logger_start();
        break;
    case BUS1_CMD_LOGGER_STOP:
        err = bus_app_logger_stop();
        break;
    case BUS1_CMD_SET_LISTEN:
        err = bus_app_set_listen(c->b0 != 0);
        break;
    case BUS1_CMD_CAN_SEND:
        err = bus_can_send(c->u0, c->b0 != 0, c->data, c->dlen);
        break;
    case BUS1_CMD_RS485_SEND:
        err = bus_rs485_send(c->data, c->dlen);
        break;
    case BUS1_CMD_UART_SEND:
        err = bus_uart_send(c->data, c->dlen);
        break;
    case BUS1_CMD_I2C_SCAN: {
        uint8_t found[32];
        (void)bus_i2c_scan(found, 32);
        break;
    }
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }
    ack.result = (err == ESP_OK) ? 0 : 1;
    send_pkt(BUS1_TYPE_ACK, &ack, sizeof(ack));
}

static void stream_task(void *arg)
{
    (void)arg;
    uint8_t rxbuf[256];
    bus_frame_t frames[16];

    while (1) {
        /* RX commands */
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(s_sock, rxbuf, sizeof(rxbuf), MSG_DONTWAIT,
                         (struct sockaddr *)&from, &flen);
        if (n >= (int)sizeof(bus1_hdr_t)) {
            const bus1_hdr_t *h = (const bus1_hdr_t *)rxbuf;
            if (h->magic == BUS1_MAGIC) {
                if (h->type == BUS1_TYPE_HELLO && h->len >= BUS1_PAIR_TOKEN_LEN) {
                    const bus1_hello_t *hello = (const bus1_hello_t *)(rxbuf + sizeof(bus1_hdr_t));
                    if (memcmp(hello->token, BUS1_PAIR_TOKEN, BUS1_PAIR_TOKEN_LEN) == 0) {
                        s_peer = from;
                        s_peer_ok = true;
                        ESP_LOGI(TAG, "paired %s:%d", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
                        bus1_ack_t ack = {.cmd_id = 0, .result = 0};
                        send_pkt(BUS1_TYPE_ACK, &ack, sizeof(ack));
                    }
                } else if (h->type == BUS1_TYPE_CMD && s_peer_ok &&
                           h->len >= sizeof(bus1_cmd_t)) {
                    handle_cmd((const bus1_cmd_t *)(rxbuf + sizeof(bus1_hdr_t)));
                }
            }
        }

        if (s_peer_ok) {
            bus_app_status_t st;
            bus_app_get_status(&st);
            bus1_telem_t telem = {
                .rx_count = st.stats.rx_count,
                .tx_count = st.stats.tx_count,
                .err_count = st.stats.err_count,
                .drop_count = st.stats.drop_count,
                .rx_rate = st.stats.rx_rate,
                .can_on = st.can_running,
                .rs485_on = st.rs485_running,
                .uart_on = st.uart_running,
                .i2c_on = st.i2c_running,
                .spi_on = st.spi_running,
                .recording = st.recording,
                .allow_tx = st.allow_tx,
                .listen_only = st.listen_only,
                .can_baud = st.can_baud,
                .rs485_baud = st.rs485_baud,
                .uart_baud = st.uart_baud,
            };
            send_pkt(BUS1_TYPE_TELEM, &telem, sizeof(telem));

            size_t nf = bus_capture_copy_since(&s_frame_seq, frames, 16);
            for (size_t i = 0; i < nf; i++) {
                bus1_frame_t bf = {
                    .t_ms = frames[i].t_ms,
                    .src = frames[i].src,
                    .dir = frames[i].dir,
                    .flags = frames[i].flags,
                    .len = frames[i].len,
                    .id = frames[i].id,
                };
                memcpy(bf.data, frames[i].data, 64);
                send_pkt(BUS1_TYPE_FRAME, &bf, sizeof(bf));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t bus_stream_start(void)
{
    if (s_sock >= 0) {
        return ESP_OK;
    }
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_sock < 0) {
        return ESP_FAIL;
    }
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(BUS1_UDP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }
    int yes = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    xTaskCreate(stream_task, "bus_stream", 6144, NULL, 5, &s_task);
    ESP_LOGI(TAG, "BUS1 UDP :%d", BUS1_UDP_PORT);
    return ESP_OK;
}
