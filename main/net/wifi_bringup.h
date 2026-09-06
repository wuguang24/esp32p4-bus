/**
 * @file wifi_bringup.h
 * @brief ESP32-P4 Hosted WiFi（STA + SoftAP 并存）
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_netif_types.h"
#include "wifi_nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool started;
    bool sta_connected;
    bool sta_got_ip;
    bool ap_started;
    bool ap_got_ip;
    esp_ip4_addr_t sta_ip;
    esp_ip4_addr_t sta_netmask;
    esp_ip4_addr_t sta_gw;
    esp_ip4_addr_t ap_ip;
    esp_ip4_addr_t ap_netmask;
    esp_ip4_addr_t ap_gw;
    char sta_ssid[WIFI_NVS_SSID_MAX];
    char ap_ssid[WIFI_NVS_SSID_MAX];
} wifi_status_t;

/** 初始化 Hosted WiFi；失败不阻塞总线/UI。 */
esp_err_t wifi_bringup_start(void);

/** STA 或 AP 任一拿到 IP 即可用于 BUS1。 */
bool wifi_bringup_ready(void);

/** WiFi 可用。 */
bool net_any_ready(void);

void wifi_bringup_get_status(wifi_status_t *out);

/** 从 NVS 加载并应用；STA 为空 SSID 则仅 AP。 */
esp_err_t wifi_bringup_apply_cfg(const wifi_nvs_cfg_t *cfg);

/** 保存 NVS 并重新连接（同步，可能阻塞数百 ms）。 */
esp_err_t wifi_bringup_save_and_apply(const wifi_nvs_cfg_t *cfg);

/** 投递到后台任务异步重配；队列满返回 ESP_FAIL。 */
esp_err_t wifi_bringup_request_save_and_apply(const wifi_nvs_cfg_t *cfg);

/** 后台 WiFi stop/start 重配进行中。 */
bool wifi_bringup_reconfig_busy(void);

#ifdef __cplusplus
}
#endif
