/**
 * @file bus_rs485.c
 * @brief UART1 + GPIO32 DE/RE 的 RS485 收发
 */

#include "bus_rs485.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "bus_capture.h"
#include "bus_framer.h"

static const char *TAG = "bus_rs485";

static bool s_running;
static uint32_t s_baud = BUS_RS485_BAUD_DEFAULT;
static bus_rs485_parity_t s_parity = BUS_RS485_PARITY_NONE;
static TaskHandle_t s_rx_task;
static volatile bool s_rx_stop;
static SemaphoreHandle_t s_tx_mutex;

static bus_rs485_frame_cb_t s_rx_cb;
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

static void bus_rs485_set_de(bool tx_enable)
{
    gpio_set_level(BUS_RS485_DE_RE_GPIO, tx_enable ? 1 : 0);
}

static uart_parity_t bus_rs485_map_parity(bus_rs485_parity_t p)
{
    switch (p) {
    case BUS_RS485_PARITY_EVEN: return UART_PARITY_EVEN;
    case BUS_RS485_PARITY_ODD:  return UART_PARITY_ODD;
    default:                    return UART_PARITY_DISABLE;
    }
}

static void bus_rs485_emit_rx(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }
    bus_frame_t f;
    memset(&f, 0, sizeof(f));
    bus_frame_stamp(&f);
    f.src = BUS_SRC_RS485;
    f.dir = BUS_DIR_RX;
    f.id = 0;
    if (len > BUS_RS485_FRAME_MAX) {
        len = BUS_RS485_FRAME_MAX;
        f.flags |= BUS_FLAG_ERR;
    }
    f.len = (uint8_t)len;
    f.dlc = f.len;
    memcpy(f.data, buf, len);
    /* 站号：若首字节像 Modbus 地址则写入 id */
    f.id = buf[0];

    (void)bus_capture_push(&f);
    if (s_rx_cb) {
        s_rx_cb(&f, s_rx_cb_user);
    }
}

static void bus_rs485_framer_emit(const uint8_t *data, size_t len, void *user)
{
    (void)user;
    bus_rs485_emit_rx(data, len);
}

static void bus_rs485_rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[BUS_RS485_FRAME_MAX];

    while (!s_rx_stop) {
        int n = uart_read_bytes(BUS_RS485_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (n > 0) {
            bus_framer_feed(&s_framer, buf, (size_t)n, bus_rs485_framer_emit, NULL);
        }
    }
    s_rx_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t bus_rs485_start(uint32_t baud, bus_rs485_parity_t parity)
{
    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (baud < BUS_RS485_BAUD_MIN || baud > BUS_RS485_BAUD_MAX) {
        ESP_LOGE(TAG, "baud %lu out of range (max %u)", (unsigned long)baud, BUS_RS485_BAUD_MAX);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_tx_mutex == NULL) {
        s_tx_mutex = xSemaphoreCreateMutex();
        if (s_tx_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BUS_RS485_DE_RE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    bus_rs485_set_de(false); /* 默认接收态 DE=0 */

    uart_config_t cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = bus_rs485_map_parity(parity),
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    err = uart_driver_install(BUS_RS485_UART_NUM, BUS_RS485_RX_BUF_SIZE,
                              BUS_RS485_TX_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_param_config(BUS_RS485_UART_NUM, &cfg);
    if (err != ESP_OK) {
        uart_driver_delete(BUS_RS485_UART_NUM);
        return err;
    }
    err = uart_set_pin(BUS_RS485_UART_NUM, BUS_RS485_TX_GPIO, BUS_RS485_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        uart_driver_delete(BUS_RS485_UART_NUM);
        return err;
    }
    (void)uart_set_rx_timeout(BUS_RS485_UART_NUM, BUS_RS485_RX_TIMEOUT_SYM);

    ensure_framer();
    bus_framer_reset(&s_framer);

    s_baud = baud;
    s_parity = parity;
    s_rx_stop = false;
    s_running = true;

    if (xTaskCreate(bus_rs485_rx_task, "bus_rs485_rx", 3072, NULL, 5, &s_rx_task) != pdPASS) {
        s_running = false;
        uart_driver_delete(BUS_RS485_UART_NUM);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "started baud=%lu parity=%d TX=%d RX=%d DE/RE=%d",
             (unsigned long)baud, (int)parity,
             (int)BUS_RS485_TX_GPIO, (int)BUS_RS485_RX_GPIO, (int)BUS_RS485_DE_RE_GPIO);
    return ESP_OK;
}

esp_err_t bus_rs485_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    s_rx_stop = true;
    for (int i = 0; i < 50 && s_rx_task != NULL; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    uart_driver_delete(BUS_RS485_UART_NUM);
    bus_rs485_set_de(false);
    s_running = false;
    ESP_LOGI(TAG, "stopped");
    return ESP_OK;
}

bool bus_rs485_is_running(void)
{
    return s_running;
}

uint32_t bus_rs485_get_baud(void)
{
    return s_baud;
}

bus_rs485_parity_t bus_rs485_get_parity(void)
{
    return s_parity;
}

void bus_rs485_set_allow_tx(bool allow)
{
    s_allow_tx = allow;
}

bool bus_rs485_get_allow_tx(void)
{
    return s_allow_tx;
}

esp_err_t bus_rs485_send(const uint8_t *buf, size_t len)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_allow_tx) {
        ESP_LOGW(TAG, "TX blocked: allow_tx=false");
        return ESP_ERR_INVALID_STATE;
    }
    if (buf == NULL || len == 0 || len > BUS_RS485_FRAME_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_tx_mutex == NULL || xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    bus_rs485_set_de(true);
    /* 方向切换建立时间（约 1 字符） */
    esp_rom_delay_us(50);

    int written = uart_write_bytes(BUS_RS485_UART_NUM, (const char *)buf, len);
    esp_err_t err = ESP_OK;
    if (written < 0 || (size_t)written != len) {
        err = ESP_FAIL;
    } else {
        err = uart_wait_tx_done(BUS_RS485_UART_NUM, pdMS_TO_TICKS(500));
    }

    esp_rom_delay_us(20);
    bus_rs485_set_de(false);
    xSemaphoreGive(s_tx_mutex);

    if (err == ESP_OK) {
        bus_frame_t f;
        memset(&f, 0, sizeof(f));
        bus_frame_stamp(&f);
        f.src = BUS_SRC_RS485;
        f.dir = BUS_DIR_TX;
        f.id = buf[0];
        f.len = (uint8_t)len;
        f.dlc = f.len;
        memcpy(f.data, buf, len);
        (void)bus_capture_push(&f);
    }
    return err;
}

void bus_rs485_set_rx_cb(bus_rs485_frame_cb_t cb, void *user)
{
    s_rx_cb = cb;
    s_rx_cb_user = user;
}

void bus_rs485_set_framer(const bus_framer_cfg_t *cfg)
{
    if (!cfg) {
        return;
    }
    ensure_framer();
    bus_framer_set_cfg(&s_framer, cfg);
}

void bus_rs485_get_framer(bus_framer_cfg_t *cfg)
{
    if (!cfg) {
        return;
    }
    ensure_framer();
    bus_framer_get_cfg(&s_framer, cfg);
}
