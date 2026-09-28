/**
 * @file bus_cli_io.h
 * @brief USB Serial/JTAG + TCP:2323 文本 CLI 输入
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bus_cli_io_start(void);

#ifdef __cplusplus
}
#endif
