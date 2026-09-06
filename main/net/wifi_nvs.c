/**
 * @file wifi_nvs.c
 */

#include "wifi_nvs.h"

#include "nvs.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "wifi_nvs";
static const char *NS = "wifi_cfg";
static const char *KEY = "blob";

#define WIFI_NVS_MAGIC  0x57494649u  /* 'WIFI' */
#define WIFI_NVS_VER    1u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    char sta_ssid[WIFI_NVS_SSID_MAX];
    char sta_pass[WIFI_NVS_PASS_MAX];
    char ap_ssid[WIFI_NVS_SSID_MAX];
    char ap_pass[WIFI_NVS_PASS_MAX];
    uint8_t ap_channel;
    uint8_t sta_enabled;
    uint8_t ap_enabled;
    uint8_t rsv;
} wifi_nvs_blob_t;

static void copy_str(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src ? src : "");
}

static void defaults(wifi_nvs_cfg_t *d)
{
    memset(d, 0, sizeof(*d));
    copy_str(d->ap_ssid, sizeof(d->ap_ssid), "ESP32P4-BUS");
    copy_str(d->ap_pass, sizeof(d->ap_pass), "12345678");
    d->ap_channel = 6;
    d->sta_enabled = true;
    d->ap_enabled = true;
}

void wifi_nvs_init(void)
{
    ESP_LOGI(TAG, "NVS 命名空间 %s", NS);
}

bool wifi_nvs_load(wifi_nvs_cfg_t *out)
{
    if (!out) {
        return false;
    }
    defaults(out);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }

    wifi_nvs_blob_t blob;
    size_t len = sizeof(blob);
    esp_err_t err = nvs_get_blob(h, KEY, &blob, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(blob)) {
        return false;
    }
    if (blob.magic != WIFI_NVS_MAGIC || blob.version != WIFI_NVS_VER) {
        return false;
    }

    memcpy(out->sta_ssid, blob.sta_ssid, sizeof(out->sta_ssid));
    memcpy(out->sta_pass, blob.sta_pass, sizeof(out->sta_pass));
    memcpy(out->ap_ssid, blob.ap_ssid, sizeof(out->ap_ssid));
    memcpy(out->ap_pass, blob.ap_pass, sizeof(out->ap_pass));
    out->ap_channel = blob.ap_channel ? blob.ap_channel : 6;
    out->sta_enabled = blob.sta_enabled != 0;
    out->ap_enabled = blob.ap_enabled != 0;

    /* 从 FOC 工程迁来的 NVS：一次性改名，避免热点仍显示 ESP32P4-FOC */
    if (strcmp(out->ap_ssid, "ESP32P4-FOC") == 0 ||
        strstr(out->ap_ssid, "FOC") != NULL) {
        copy_str(out->ap_ssid, sizeof(out->ap_ssid), "ESP32P4-BUS");
        if (wifi_nvs_save(out)) {
            ESP_LOGW(TAG, "AP SSID 已从 FOC 迁移为 ESP32P4-BUS");
        }
    }
    return true;
}

bool wifi_nvs_save(const wifi_nvs_cfg_t *in)
{
    if (!in) {
        return false;
    }

    wifi_nvs_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = WIFI_NVS_MAGIC;
    blob.version = WIFI_NVS_VER;
    copy_str(blob.sta_ssid, sizeof(blob.sta_ssid), in->sta_ssid);
    copy_str(blob.sta_pass, sizeof(blob.sta_pass), in->sta_pass);
    copy_str(blob.ap_ssid, sizeof(blob.ap_ssid), in->ap_ssid);
    copy_str(blob.ap_pass, sizeof(blob.ap_pass), in->ap_pass);
    blob.ap_channel = in->ap_channel ? in->ap_channel : 6;
    blob.sta_enabled = in->sta_enabled ? 1u : 0u;
    blob.ap_enabled = in->ap_enabled ? 1u : 0u;

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
        ESP_LOGE(TAG, "保存失败 %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "WiFi 配置已保存");
    return true;
}
