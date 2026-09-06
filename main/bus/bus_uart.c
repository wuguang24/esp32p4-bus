/**
 * @file bus_uart.c
 * @brief TTL UART2 监听/发送（GPIO9/10）
 */

#include "bus_uart.h"

#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bus_capture.h"
#include "bus_framer.h"
#include "bus_pins.h"

static const char *TAG = "bus_uart";

static bool s_running;
static uint32_t s_baud = BUS_UART_BAUD_DEFAULT;
static bus_uart_parity_t s_parity = BUS_UART_PARITY_NONE;
static bus_uart_stop_t s_stop = BUS_UART_STOP_1;
static TaskHandle_t s_rx_task;
static volatile bool s_rx_stop;
static bus_uart_frame_cb_t s_rx_cb;
static void *s_rx_cb_user;
static bool s_allow_tx;
static bus_framer_t s_framer;
static bool s_framer_ready;

static void ensure_framer(void)
{
    if (s_framer_ready) {
        return;
    }
    bus_framer_cfg_t def = {
        .mode = BUS_FR_IDLE,
        .fixed_len = 8,
        .sof = {0xAA},
        .sof_n = 0,
        .eof = {0x55},
        .eof_n = 0,
    };
    bus_framer_init(&s_framer, &def);
    s_framer_ready = true;
}

void bus_uart_set_allow_tx(bool allow)
{
    s_allow_tx = allow;
}

static uart_parity_t map_parity(bus_uart_parity_t p)
{
    switch (p) {
    case BUS_UART_PARITY_EVEN: return UART_PARITY_EVEN;
    case BUS_UART_PARITY_ODD:  return UART_PARITY_ODD;
    default:                   return UART_PARITY_DISABLE;
    }
}

static uart_stop_bits_t map_stop(bus_uart_stop_t s)
{
    return (s == BUS_UART_STOP_2) ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
}

static void push_rx(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return;
    }
    if (len > BUS_UART_FRAME_MAX) {
        len = BUS_UART_FRAME_MAX;
    }
    bus_frame_t f = {0};
    bus_frame_stamp(&f);
    f.src = BUS_SRC_UART;
    f.dir = BUS_DIR_RX;
    f.len = (uint8_t)len;
    f.dlc = f.len;
    memcpy(f.data, data, len);
    (void)bus_capture_push(&f);
    if (s_rx_cb) {
        s_rx_cb(&f, s_rx_cb_user);
    }
}

static void framer_emit(const uint8_t *data, size_t len, void *user)
{
    (void)user;
    push_rx(data, len);
}

static void rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[BUS_UART_FRAME_MAX];
    while (!s_rx_stop) {
        int n = uart_read_bytes(BUS_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (n > 0) {
            bus_framer_feed(&s_framer, buf, (size_t)n, framer_emit, NULL);
        }
    }
    s_rx_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t bus_uart_start(uint32_t baud, bus_uart_parity_t parity, bus_uart_stop_t stop)
{
    if (s_running) {
        (void)bus_uart_stop();
    }
    if (baud == 0 || baud > BUS_UART_BAUD_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    ensure_framer();
    bus_framer_reset(&s_framer);
    s_baud = baud;
    s_parity = parity;
    s_stop = stop;

    uart_config_t cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = map_parity(parity),
        .stop_bits = map_stop(stop),
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(BUS_UART_NUM, BUS_UART_RX_BUF_SIZE, 512, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    ESP_ERROR_CHECK(uart_param_config(BUS_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(BUS_UART_NUM, BUS_UART_TX_GPIO, BUS_UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_set_rx_timeout(BUS_UART_NUM, BUS_UART_RX_TIMEOUT_SYM));

    s_rx_stop = false;
    BaseType_t ok = xTaskCreate(rx_task, "bus_uart_rx", 3072, NULL, 8, &s_rx_task);
    if (ok != pdPASS) {
        uart_driver_delete(BUS_UART_NUM);
        return ESP_ERR_NO_MEM;
    }
    s_running = true;
    ESP_LOGI(TAG, "UART2 start %lu baud P%d S%d TX%d RX%d", (unsigned long)baud,
             (int)parity, (int)stop, (int)BUS_UART_TX_GPIO, (int)BUS_UART_RX_GPIO);
    return ESP_OK;
}

esp_err_t bus_uart_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    s_rx_stop = true;
    for (int i = 0; i < 50 && s_rx_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    uart_driver_delete(BUS_UART_NUM);
    s_running = false;
    return ESP_OK;
}

bool bus_uart_is_running(void)
{
    return s_running;
}

uint32_t bus_uart_get_baud(void)
{
    return s_baud;
}

bus_uart_parity_t bus_uart_get_parity(void)
{
    return s_parity;
}

bus_uart_stop_t bus_uart_get_stop(void)
{
    return s_stop;
}

esp_err_t bus_uart_send(const uint8_t *buf, size_t len)
{
    if (!s_running || buf == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_allow_tx) {
        return ESP_ERR_INVALID_STATE;
    }
    int w = uart_write_bytes(BUS_UART_NUM, buf, len);
    if (w < 0) {
        return ESP_FAIL;
    }
    (void)uart_wait_tx_done(BUS_UART_NUM, pdMS_TO_TICKS(200));

    bus_frame_t f = {0};
    bus_frame_stamp(&f);
    f.src = BUS_SRC_UART;
    f.dir = BUS_DIR_TX;
    f.len = (uint8_t)((len > 64) ? 64 : len);
    f.dlc = f.len;
    memcpy(f.data, buf, f.len);
    (void)bus_capture_push(&f);
    return ESP_OK;
}

void bus_uart_set_rx_cb(bus_uart_frame_cb_t cb, void *user)
{
    s_rx_cb = cb;
    s_rx_cb_user = user;
}

void bus_uart_set_framer(const bus_framer_cfg_t *cfg)
{
    if (!cfg) {
        return;
    }
    ensure_framer();
    bus_framer_set_cfg(&s_framer, cfg);
}

void bus_uart_get_framer(bus_framer_cfg_t *cfg)
{
    if (!cfg) {
        return;
    }
    ensure_framer();
    bus_framer_get_cfg(&s_framer, cfg);
}
