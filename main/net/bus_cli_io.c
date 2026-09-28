/**
 * @file bus_cli_io.c
 * @brief 从 USB Serial/JTAG 与 TCP:2323 读行并执行 bus_cli
 */

#include "bus_cli_io.h"

#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "bus_cli.h"

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED || CONFIG_USJ_ENABLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#define BUS_CLI_HAS_USJ 1
#else
#define BUS_CLI_HAS_USJ 0
#endif

static const char *TAG = "bus_cli_io";

typedef struct {
    int fd;
} tcp_ctx_t;

static void cli_write_fd(const char *line, void *user)
{
    tcp_ctx_t *c = (tcp_ctx_t *)user;
    if (!c || c->fd < 0 || !line) {
        return;
    }
    send(c->fd, line, strlen(line), 0);
    send(c->fd, "\r\n", 2, 0);
}

#if BUS_CLI_HAS_USJ
static void usj_out(const char *line, void *user)
{
    (void)user;
    if (!line) {
        return;
    }
    usb_serial_jtag_write_bytes(line, strlen(line), pdMS_TO_TICKS(50));
    usb_serial_jtag_write_bytes("\r\n", 2, pdMS_TO_TICKS(20));
}

static void usj_task(void *arg)
{
    (void)arg;
    char line[160];
    size_t pos = 0;
    uint8_t ch;
    const char *banner = "\r\nESP32P4 BUS CLI (USB). Type help\r\n> ";
    usb_serial_jtag_write_bytes(banner, strlen(banner), pdMS_TO_TICKS(100));

    while (1) {
        int n = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(100));
        if (n <= 0) {
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (pos == 0) {
                usb_serial_jtag_write_bytes("> ", 2, pdMS_TO_TICKS(20));
                continue;
            }
            line[pos] = '\0';
            pos = 0;
            usb_serial_jtag_write_bytes("\r\n", 2, pdMS_TO_TICKS(20));
            (void)bus_cli_exec(line, usj_out, NULL);
            usb_serial_jtag_write_bytes("> ", 2, pdMS_TO_TICKS(20));
            continue;
        }
        if (ch == 0x08 || ch == 0x7F) {
            if (pos > 0) {
                pos--;
                usb_serial_jtag_write_bytes("\b \b", 3, pdMS_TO_TICKS(20));
            }
            continue;
        }
        if (pos + 1 < sizeof(line) && ch >= 0x20) {
            line[pos++] = (char)ch;
            usb_serial_jtag_write_bytes(&ch, 1, pdMS_TO_TICKS(20));
        }
    }
}
#endif

static void tcp_client_task(void *arg)
{
    int fd = (int)(intptr_t)arg;
    tcp_ctx_t ctx = {.fd = fd};
    const char *banner = "ESP32P4 BUS CLI (TCP:2323). Type help\r\n> ";
    send(fd, banner, strlen(banner), 0);

    char line[160];
    size_t pos = 0;
    char ch;
    while (1) {
        int n = recv(fd, &ch, 1, 0);
        if (n <= 0) {
            break;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            line[pos] = '\0';
            pos = 0;
            (void)bus_cli_exec(line, cli_write_fd, &ctx);
            send(fd, "> ", 2, 0);
            continue;
        }
        if (pos + 1 < sizeof(line)) {
            line[pos++] = ch;
        }
    }
    close(fd);
    vTaskDelete(NULL);
}

static void tcp_server_task(void *arg)
{
    (void)arg;
    int srv = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (srv < 0) {
        ESP_LOGE(TAG, "socket fail");
        vTaskDelete(NULL);
        return;
    }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(2323),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind :2323 fail");
        close(srv);
        vTaskDelete(NULL);
        return;
    }
    listen(srv, 2);
    ESP_LOGI(TAG, "TCP CLI :2323");
    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(srv, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) {
            continue;
        }
        xTaskCreate(tcp_client_task, "cli_tcp_c", 4096, (void *)(intptr_t)cfd, 4, NULL);
    }
}

esp_err_t bus_cli_io_start(void)
{
#if BUS_CLI_HAS_USJ
    /* 驱动通常已由 console 初始化；失败则再装一次 */
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t e = usb_serial_jtag_driver_install(&cfg);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "USJ install: %s (CLI via TCP/HTTP still ok)", esp_err_to_name(e));
    } else {
        xTaskCreate(usj_task, "cli_usj", 4096, NULL, 5, NULL);
        ESP_LOGI(TAG, "USB Serial/JTAG CLI ready");
    }
#endif
    xTaskCreate(tcp_server_task, "cli_tcp", 3072, NULL, 4, NULL);
    return ESP_OK;
}
