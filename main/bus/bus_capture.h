/**
 * @file bus_capture.h
 * @brief 统一捕获环缓 + 分通道吞吐/负载统计 + 历史采样
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "bus_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/** PSRAM 环形缓冲容量（帧数） */
#define BUS_CAPTURE_CAP         2048
#define BUS_SRC_COUNT           5
#define BUS_RATE_HIST_LEN       48    /* ~2s @ 40ms UI tick */
/** UI 列表可见行 */
#define BUS_LIST_ROWS           40
#define BUS_SNAPSHOT_MAX        64

/** 快照过滤：flags=0 且 id_en=false 表示不过滤 */
#define BUS_FILT_RX             (1u << 0)
#define BUS_FILT_TX             (1u << 1)
#define BUS_FILT_ERR            (1u << 2)

typedef struct {
    uint8_t flags;       /* BUS_FILT_* 组合；0=方向/错误不过滤 */
    bool id_en;
    uint32_t id;
} bus_capture_filter_t;

typedef struct {
    uint32_t rx_count;
    uint32_t tx_count;
    uint32_t err_count;
    uint32_t drop_count;
    float rx_rate;          /* 全局接收帧/秒 */
    uint64_t total_pushed;
    uint32_t ring_used;
    uint32_t ring_cap;
} bus_capture_stats_t;

/** 单通道实时统计 */
typedef struct {
    uint32_t rx_count;
    uint32_t tx_count;
    uint32_t err_count;
    float fps;              /* 帧/秒（RX+TX） */
    float bytes_per_s;      /* 字节/秒 */
    float load_pct;         /* 估算总线负载 0~100 */
    uint8_t busy_level;     /* 0空闲 1中 2忙 */
} bus_ch_stats_t;

#define BUS_TOP_ID_N            5

typedef struct {
    uint32_t id;
    uint32_t count;
} bus_id_stat_t;

esp_err_t bus_capture_init(void);
esp_err_t bus_capture_push(const bus_frame_t *frame);

size_t bus_capture_snapshot_recent(bus_frame_t *out, size_t max_n);
/** 只取指定通道最近帧（最旧在前） */
size_t bus_capture_snapshot_src(uint8_t src, bus_frame_t *out, size_t max_n);
/** 带过滤的通道快照（最旧在前） */
size_t bus_capture_snapshot_src_f(uint8_t src, bus_frame_t *out, size_t max_n,
                                  const bus_capture_filter_t *filt);
size_t bus_capture_copy_since(uint64_t *inout_seq, bus_frame_t *out, size_t max_n);

void bus_capture_get_stats(bus_capture_stats_t *out);
void bus_capture_get_ch_stats(bus_ch_stats_t out[BUS_SRC_COUNT]);

/** 设置通道波特率/时钟，用于负载估算（I2C/SPI 传 Hz） */
void bus_capture_set_link_rate(uint8_t src, uint32_t rate_hz);

/**
 * @brief 周期性采样（建议 25ms 调用）
 * 结算窗口速率、写入负载历史
 */
void bus_capture_tick(void);

/** 负载历史世代号（每次 tick 结算 +1），UI 可据此跳过重复刷图 */
uint32_t bus_capture_hist_seq(void);

/** 取某通道负载历史（最旧在前），返回点数 */
size_t bus_capture_get_load_hist(uint8_t src, uint8_t *out, size_t max_n);

/** 通道 ID/站号 TopN（按计数降序） */
size_t bus_capture_get_top_ids(uint8_t src, bus_id_stat_t *out, size_t max_n);

void bus_capture_clear(void);

#ifdef __cplusplus
}
#endif
