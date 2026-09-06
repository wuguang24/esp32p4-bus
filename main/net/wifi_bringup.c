/**
 * @file wifi_bringup.c
 * @brief ESP-Hosted (C6 SDIO) WiFi APSTA，参考 07_WIFI_SDCARD_SHARE
 */

#include "wifi_bringup.h"
#include "wifi_nvs.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "wifi_br";

static void copy_str(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src ? src : "");
}

#define WIFI_STA_CONNECTED_BIT  BIT0
#define WIFI_STA_GOT_IP_BIT     BIT1
#define WIFI_AP_GOT_IP_BIT      BIT2

static EventGroupHandle_t s_wifi_ev;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static wifi_status_t s_st;
static wifi_nvs_cfg_t s_cfg;
static SemaphoreHandle_t s_lock;
static bool s_started;
static QueueHandle_t s_reconfig_q;
static volatile bool s_reconfig_busy;
static esp_timer_handle_t s_reconnect_timer;
static uint8_t s_sta_retry;

static void sta_reconnect_timer_cb(void *arg)
{
    (void)arg;
    if (s_cfg.sta_enabled && s_cfg.sta_ssid[0]) {
        esp_wifi_connect();
    }
}

static void schedule_sta_reconnect(void)
{
    if (!s_reconnect_timer) {
        return;
    }
    if (s_sta_retry < 5) {
        s_sta_retry++;
    }
    uint64_t delay_us = 500000ULL << (s_sta_retry - 1);
    if (delay_us > 16000000ULL) {
        delay_us = 16000000ULL;
    }
    esp_timer_stop(s_reconnect_timer);
    esp_timer_start_once(s_reconnect_timer, delay_us);
}

static void wifi_reconfig_task(void *arg)
{
    (void)arg;
    wifi_nvs_cfg_t cfg;
    for (;;) {
        if (xQueueReceive(s_reconfig_q, &cfg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_reconfig_busy = true;
        esp_err_t err = wifi_bringup_save_and_apply(&cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "异步 WiFi 重配失败 %s", esp_err_to_name(err));
        }
        s_reconfig_busy = false;
    }
}

static void status_lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void status_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static void copy_ip(esp_ip4_addr_t *dst, const esp_ip4_addr_t *src)
{
    if (dst && src) {
        *dst = *src;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA 启动");
            if (s_cfg.sta_enabled && s_cfg.sta_ssid[0]) {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t *ev = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "STA 已连 AP: %s", (const char *)ev->ssid);
            s_sta_retry = 0;
            if (s_reconnect_timer) {
                esp_timer_stop(s_reconnect_timer);
            }
            status_lock();
            s_st.sta_connected = true;
            copy_str(s_st.sta_ssid, sizeof(s_st.sta_ssid), (const char *)ev->ssid);
            status_unlock();
            xEventGroupSetBits(s_wifi_ev, WIFI_STA_CONNECTED_BIT);
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "STA 断开，退避重连…");
            status_lock();
            s_st.sta_connected = false;
            s_st.sta_got_ip = false;
            status_unlock();
            xEventGroupClearBits(s_wifi_ev, WIFI_STA_CONNECTED_BIT | WIFI_STA_GOT_IP_BIT);
            if (s_cfg.sta_enabled && s_cfg.sta_ssid[0]) {
                schedule_sta_reconnect();
            }
            break;
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "SoftAP 已启动 SSID=%s", s_cfg.ap_ssid);
            status_lock();
            s_st.ap_started = true;
            status_unlock();
            break;
        case WIFI_EVENT_AP_STOP:
            status_lock();
            s_st.ap_started = false;
            s_st.ap_got_ip = false;
            status_unlock();
            xEventGroupClearBits(s_wifi_ev, WIFI_AP_GOT_IP_BIT);
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "有设备连入 SoftAP");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "设备离开 SoftAP");
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "STA IP " IPSTR, IP2STR(&ev->ip_info.ip));
            status_lock();
            s_st.sta_got_ip = true;
            copy_ip(&s_st.sta_ip, &ev->ip_info.ip);
            copy_ip(&s_st.sta_netmask, &ev->ip_info.netmask);
            copy_ip(&s_st.sta_gw, &ev->ip_info.gw);
            status_unlock();
            xEventGroupSetBits(s_wifi_ev, WIFI_STA_GOT_IP_BIT);
        } else if (event_id == IP_EVENT_ASSIGNED_IP_TO_CLIENT) {
            ip_event_assigned_ip_to_client_t *ev = (ip_event_assigned_ip_to_client_t *)event_data;
            ESP_LOGI(TAG, "AP 分配 IP 给客户端 " IPSTR, IP2STR(&ev->ip));
        } else if (event_id == IP_EVENT_GOT_IP6) {
            /* ignore */
        }
    }
}

static esp_err_t apply_wifi_config(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (s_cfg.ap_enabled && s_cfg.sta_enabled && s_cfg.sta_ssid[0]) {
        mode = WIFI_MODE_APSTA;
    } else if (s_cfg.ap_enabled) {
        mode = WIFI_MODE_AP;
    } else if (s_cfg.sta_enabled && s_cfg.sta_ssid[0]) {
        mode = WIFI_MODE_STA;
    } else {
        mode = WIFI_MODE_AP;
        s_cfg.ap_enabled = true;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(mode), TAG, "set_mode");

    if (s_cfg.ap_enabled) {
        wifi_config_t ap = {0};
        copy_str((char *)ap.ap.ssid, sizeof(ap.ap.ssid), s_cfg.ap_ssid);
        ap.ap.ssid_len = (uint8_t)strlen((char *)ap.ap.ssid);
        copy_str((char *)ap.ap.password, sizeof(ap.ap.password), s_cfg.ap_pass);
        ap.ap.channel = s_cfg.ap_channel ? s_cfg.ap_channel : 6;
        ap.ap.max_connection = 4;
        ap.ap.authmode = (strlen(s_cfg.ap_pass) >= 8) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "ap cfg");
        status_lock();
        copy_str(s_st.ap_ssid, sizeof(s_st.ap_ssid), s_cfg.ap_ssid);
        status_unlock();
    }

    if (mode == WIFI_MODE_APSTA || mode == WIFI_MODE_STA) {
        if (s_cfg.sta_ssid[0]) {
            wifi_config_t sta = {0};
            copy_str((char *)sta.sta.ssid, sizeof(sta.sta.ssid), s_cfg.sta_ssid);
            copy_str((char *)sta.sta.password, sizeof(sta.sta.password), s_cfg.sta_pass);
            sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta), TAG, "sta cfg");
        }
    }

    return ESP_OK;
}

static void read_ap_ip(void)
{
    if (!s_ap_netif) {
        return;
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_ap_netif, &ip) == ESP_OK) {
        status_lock();
        s_st.ap_got_ip = true;
        copy_ip(&s_st.ap_ip, &ip.ip);
        copy_ip(&s_st.ap_netmask, &ip.netmask);
        copy_ip(&s_st.ap_gw, &ip.gw);
        status_unlock();
        xEventGroupSetBits(s_wifi_ev, WIFI_AP_GOT_IP_BIT);
    }
}

esp_err_t wifi_bringup_apply_cfg(const wifi_nvs_cfg_t *cfg)
{
    if (!cfg || !s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_cfg = *cfg;
    esp_wifi_stop();
    esp_err_t err = apply_wifi_config();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err == ESP_OK && s_cfg.ap_enabled) {
        vTaskDelay(pdMS_TO_TICKS(100));
        read_ap_ip();
    }
    return err;
}

esp_err_t wifi_bringup_save_and_apply(const wifi_nvs_cfg_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!wifi_nvs_save(cfg)) {
        return ESP_FAIL;
    }
    return wifi_bringup_apply_cfg(cfg);
}

esp_err_t wifi_bringup_request_save_and_apply(const wifi_nvs_cfg_t *cfg)
{
    if (!cfg || !s_started || s_reconfig_q == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueOverwrite(s_reconfig_q, cfg) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool wifi_bringup_reconfig_busy(void)
{
    return s_reconfig_busy;
}

esp_err_t wifi_bringup_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    memset(&s_st, 0, sizeof(s_st));
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");

    s_wifi_ev = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_ev, ESP_ERR_NO_MEM, TAG, "ev group");

    s_reconfig_q = xQueueCreate(1, sizeof(wifi_nvs_cfg_t));
    ESP_RETURN_ON_FALSE(s_reconfig_q, ESP_ERR_NO_MEM, TAG, "reconfig q");

    const esp_timer_create_args_t tmr_args = {
        .callback = sta_reconnect_timer_cb,
        .name = "wifi_sta_re",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&tmr_args, &s_reconnect_timer), TAG, "reconn tmr");

    BaseType_t task_ok = xTaskCreate(wifi_reconfig_task, "wifi_recfg", 4096, NULL, 3, NULL);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "reconfig task");

    wifi_nvs_init();
    if (!wifi_nvs_load(&s_cfg)) {
        wifi_nvs_save(&s_cfg);
    }

    /* netif / event loop may already be initialized */
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(s_sta_netif && s_ap_netif, ESP_FAIL, TAG, "netif");

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wcfg), TAG, "wifi_init");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   &wifi_event_handler, NULL),
                        TAG, "wifi evt");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                                   &wifi_event_handler, NULL),
                        TAG, "ip evt");

    ESP_RETURN_ON_ERROR(apply_wifi_config(), TAG, "apply");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");

    if (s_cfg.ap_enabled) {
        vTaskDelay(pdMS_TO_TICKS(150));
        read_ap_ip();
    }

    s_started = true;
    status_lock();
    s_st.started = true;
    status_unlock();

    ESP_LOGI(TAG, "WiFi APSTA 就绪 AP=%s STA=%s",
             s_cfg.ap_enabled ? s_cfg.ap_ssid : "-",
             (s_cfg.sta_enabled && s_cfg.sta_ssid[0]) ? s_cfg.sta_ssid : "-");
    return ESP_OK;
}

bool wifi_bringup_ready(void)
{
    status_lock();
    bool ok = s_started && (s_st.sta_got_ip || s_st.ap_got_ip);
    status_unlock();
    return ok;
}

bool net_any_ready(void)
{
    return wifi_bringup_ready();
}

void wifi_bringup_get_status(wifi_status_t *out)
{
    if (!out) {
        return;
    }
    status_lock();
    *out = s_st;
    status_unlock();
}
