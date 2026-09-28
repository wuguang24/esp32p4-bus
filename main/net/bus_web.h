/**
 * @file bus_web.h
 * @brief SoftAP HTTP：浏览器 CLI（http://192.168.4.1/）
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bus_web_start(void);
void bus_web_stop(void);
bool bus_web_is_running(void);

#ifdef __cplusplus
}
#endif
