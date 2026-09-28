/**
 * @file bus_cli.h
 * @brief 文本命令行：USB/Web/UDP 共用
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bus_cli_out_fn)(const char *line, void *user);

/**
 * @brief 执行一行命令，结果通过 out 逐行回调（可为 NULL 则用 ESP_LOGI）
 * @return 0 成功；非 0 失败/未知命令
 */
int bus_cli_exec(const char *line, bus_cli_out_fn out, void *user);

/** 帮助文本（多行，\\n 分隔） */
const char *bus_cli_help(void);

#ifdef __cplusplus
}
#endif
