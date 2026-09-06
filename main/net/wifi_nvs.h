/**
 * @file wifi_nvs.h
 * @brief WiFi STA/AP 凭据 NVS 持久化
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_NVS_SSID_MAX  32
#define WIFI_NVS_PASS_MAX  64

typedef struct {
    char sta_ssid[WIFI_NVS_SSID_MAX];
    char sta_pass[WIFI_NVS_PASS_MAX];
    char ap_ssid[WIFI_NVS_SSID_MAX];
    char ap_pass[WIFI_NVS_PASS_MAX];
    uint8_t ap_channel;
    bool sta_enabled;
    bool ap_enabled;
} wifi_nvs_cfg_t;

void wifi_nvs_init(void);
bool wifi_nvs_load(wifi_nvs_cfg_t *out);
bool wifi_nvs_save(const wifi_nvs_cfg_t *in);

#ifdef __cplusplus
}
#endif
