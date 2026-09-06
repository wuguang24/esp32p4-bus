/**
 * @file bus_capture.c
 * @brief 捕获环缓（PSRAM）+ 分通道 fps/负载 + 25ms 历史
 */

#include "bus_capture.h"

#include <string.h>
#include <stdint.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "bus_decode.h"

static const char *TAG = "bus_capture";

static bus_frame_t *s_ring;
static size_t s_cap;
static size_t s_head;
static size_t s_count;
static uint64_t s_seq;
static SemaphoreHandle_t s_mutex;

static uint32_t s_rx_count;
static uint32_t s_tx_count;
static uint32_t s_err_count;
static uint32_t s_drop_count;
static float s_rx_rate;

static uint32_t s_ch_rx[BUS_SRC_COUNT];
static uint32_t s_ch_tx[BUS_SRC_COUNT];
static uint32_t s_ch_err[BUS_SRC_COUNT];
static uint32_t s_link_rate[BUS_SRC_COUNT];

static int64_t s_win_start_us;
static uint32_t s_win_frames[BUS_SRC_COUNT];
static uint32_t s_win_bytes[BUS_SRC_COUNT];
static uint32_t s_win_bits[BUS_SRC_COUNT];
static uint32_t s_win_rx_all;

static bus_ch_stats_t s_ch[BUS_SRC_COUNT];

static uint8_t s_hist[BUS_SRC_COUNT][BUS_RATE_HIST_LEN];
static size_t s_hist_head;
static size_t s_hist_count;
static uint32_t s_hist_seq;

#define BUS_IDTAB_SLOTS 16
static bus_id_stat_t s_idtab[BUS_SRC_COUNT][BUS_IDTAB_SLOTS];

static int src_ok(uint8_t src)
{
    return src < BUS_SRC_COUNT;
}

static void idtab_hit(uint8_t src, uint32_t id)
{
    if (!src_ok(src)) {
        return;
    }
    int free_i = -1;
    int weak_i = 0;
    uint32_t weak_c = UINT32_MAX;
    for (int i = 0; i < BUS_IDTAB_SLOTS; i++) {
        if (s_idtab[src][i].count == 0 && free_i < 0) {
            free_i = i;
        }
        if (s_idtab[src][i].id == id && s_idtab[src][i].count > 0) {
            if (s_idtab[src][i].count < UINT32_MAX) {
                s_idtab[src][i].count++;
            }
            return;
        }
        if (s_idtab[src][i].count < weak_c) {
            weak_c = s_idtab[src][i].count;
            weak_i = i;
        }
    }
    int slot = (free_i >= 0) ? free_i : weak_i;
    s_idtab[src][slot].id = id;
    s_idtab[src][slot].count = 1;
}

static void lock(void)
{
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

static uint32_t estimate_bits(const bus_frame_t *f)
{
    uint32_t len = f->len;
    switch (f->src) {
    case BUS_SRC_CAN:
        return ((f->flags & BUS_FLAG_EXT) ? 65u : 47u) + 8u * len;
    case BUS_SRC_RS485:
    case BUS_SRC_UART:
        return len * 10u;
    case BUS_SRC_I2C:
        return 18u + len * 9u;
    case BUS_SRC_SPI:
        return len * 8u;
    default:
        return len * 8u;
    }
}

static float clamp_load(float pct)
{
    if (pct < 0.0f) {
        return 0.0f;
    }
    if (pct > 100.0f) {
        return 100.0f;
    }
    return pct;
}

static uint8_t busy_of(float load)
{
    if (load < 10.0f) {
        return 0;
    }
    if (load < 50.0f) {
        return 1;
    }
    return 2;
}

esp_err_t bus_capture_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_ring == NULL) {
        s_cap = BUS_CAPTURE_CAP;
        s_ring = heap_caps_calloc(s_cap, sizeof(bus_frame_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_ring) {
            ESP_LOGE(TAG, "ring alloc failed (%u x %u)", (unsigned)s_cap, (unsigned)sizeof(bus_frame_t));
            return ESP_ERR_NO_MEM;
        }
    }
    s_link_rate[BUS_SRC_CAN] = 500000;
    s_link_rate[BUS_SRC_RS485] = 115200;
    s_link_rate[BUS_SRC_UART] = 115200;
    s_link_rate[BUS_SRC_I2C] = 100000;
    s_link_rate[BUS_SRC_SPI] = 1000000;
    bus_capture_clear();
    ESP_LOGI(TAG, "init cap=%u hist=%d ring=%u KB PSRAM",
             (unsigned)s_cap, BUS_RATE_HIST_LEN,
             (unsigned)((s_cap * sizeof(bus_frame_t)) / 1024));
    return ESP_OK;
}

void bus_capture_set_link_rate(uint8_t src, uint32_t rate_hz)
{
    if (!src_ok(src) || rate_hz == 0) {
        return;
    }
    lock();
    s_link_rate[src] = rate_hz;
    unlock();
}

esp_err_t bus_capture_push(const bus_frame_t *frame)
{
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ring && bus_capture_init() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }

    bus_frame_t f = *frame;
    if (f.t_us == 0) {
        bus_frame_stamp(&f);
    } else if (f.t_ms == 0) {
        f.t_ms = (uint32_t)(f.t_us / 1000ULL);
    }
    bus_decode_annotate(&f);

    lock();
    if (s_count >= s_cap) {
        s_drop_count++;
    } else {
        s_count++;
    }
    s_ring[s_head] = f;
    s_head = (s_head + 1) % s_cap;
    s_seq++;

    uint8_t src = f.src;
    if (src_ok(src)) {
        uint32_t bits = estimate_bits(&f);
        s_win_frames[src]++;
        s_win_bytes[src] += f.len;
        s_win_bits[src] += bits;
        if (f.flags & BUS_FLAG_ERR) {
            s_ch_err[src]++;
            s_err_count++;
        }
        if (f.dir == BUS_DIR_TX) {
            s_ch_tx[src]++;
            s_tx_count++;
        } else {
            s_ch_rx[src]++;
            s_rx_count++;
            s_win_rx_all++;
        }
        if (!(f.flags & BUS_FLAG_ERR)) {
            idtab_hit(src, f.id);
        }
    }
    unlock();
    return ESP_OK;
}

void bus_capture_tick(void)
{
    if (!s_mutex || !s_ring) {
        return;
    }
    lock();
    int64_t now = esp_timer_get_time();
    if (s_win_start_us == 0) {
        s_win_start_us = now;
        unlock();
        return;
    }
    int64_t elapsed = now - s_win_start_us;
    if (elapsed < 35000LL) {
        unlock();
        return;
    }
    float sec = (float)elapsed / 1000000.0f;
    if (sec < 0.001f) {
        sec = 0.001f;
    }

    uint32_t rx_all = s_win_rx_all;
    for (int i = 0; i < BUS_SRC_COUNT; i++) {
        float fps = (float)s_win_frames[i] / sec;
        float bps = (float)s_win_bytes[i] / sec;
        float load = 0.0f;
        if (s_link_rate[i] > 0) {
            float bit_rate = (float)s_win_bits[i] / sec;
            load = clamp_load(100.0f * bit_rate / (float)s_link_rate[i]);
        }
        s_ch[i].rx_count = s_ch_rx[i];
        s_ch[i].tx_count = s_ch_tx[i];
        s_ch[i].err_count = s_ch_err[i];
        s_ch[i].fps = fps;
        s_ch[i].bytes_per_s = bps;
        s_ch[i].load_pct = load;
        s_ch[i].busy_level = busy_of(load);

        s_hist[i][s_hist_head] = (uint8_t)(load + 0.5f);
        s_win_frames[i] = 0;
        s_win_bytes[i] = 0;
        s_win_bits[i] = 0;
    }
    s_rx_rate = (float)rx_all / sec;
    s_win_rx_all = 0;
    s_win_start_us = now;
    s_hist_head = (s_hist_head + 1) % BUS_RATE_HIST_LEN;
    if (s_hist_count < BUS_RATE_HIST_LEN) {
        s_hist_count++;
    }
    s_hist_seq++;
    unlock();
}

uint32_t bus_capture_hist_seq(void)
{
    return s_hist_seq;
}

size_t bus_capture_snapshot_recent(bus_frame_t *out, size_t max_n)
{
    if (!out || max_n == 0 || !s_ring) {
        return 0;
    }
    lock();
    size_t n = s_count < max_n ? s_count : max_n;
    size_t start = (s_head + s_cap - n) % s_cap;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_ring[(start + i) % s_cap];
    }
    unlock();
    return n;
}

size_t bus_capture_snapshot_src(uint8_t src, bus_frame_t *out, size_t max_n)
{
    return bus_capture_snapshot_src_f(src, out, max_n, NULL);
}

static bool filt_match(const bus_frame_t *f, const bus_capture_filter_t *filt)
{
    if (!filt) {
        return true;
    }
    if (filt->flags & (BUS_FILT_RX | BUS_FILT_TX)) {
        bool want_rx = (filt->flags & BUS_FILT_RX) != 0;
        bool want_tx = (filt->flags & BUS_FILT_TX) != 0;
        if (f->dir == BUS_DIR_TX) {
            if (!want_tx) {
                return false;
            }
        } else if (!want_rx) {
            return false;
        }
    }
    if ((filt->flags & BUS_FILT_ERR) &&
        !(f->flags & (BUS_FLAG_ERR | BUS_FLAG_CRC_BAD))) {
        return false;
    }
    if (filt->id_en && f->id != filt->id) {
        return false;
    }
    return true;
}

size_t bus_capture_snapshot_src_f(uint8_t src, bus_frame_t *out, size_t max_n,
                                  const bus_capture_filter_t *filt)
{
    if (!out || max_n == 0 || !src_ok(src) || !s_ring) {
        return 0;
    }
    if (max_n > BUS_SNAPSHOT_MAX) {
        max_n = BUS_SNAPSHOT_MAX;
    }
    lock();
    bus_frame_t tmp[BUS_SNAPSHOT_MAX];
    size_t tn = 0;
    for (size_t k = 0; k < s_count && tn < max_n; k++) {
        size_t idx = (s_head + s_cap - 1 - k) % s_cap;
        if (s_ring[idx].src != src) {
            continue;
        }
        if (!filt_match(&s_ring[idx], filt)) {
            continue;
        }
        tmp[tn++] = s_ring[idx];
    }
    for (size_t i = 0; i < tn; i++) {
        out[i] = tmp[tn - 1 - i];
    }
    unlock();
    return tn;
}

size_t bus_capture_copy_since(uint64_t *inout_seq, bus_frame_t *out, size_t max_n)
{
    if (!inout_seq || !out || max_n == 0 || !s_ring) {
        return 0;
    }
    lock();
    uint64_t last = *inout_seq;
    if (s_count == 0) {
        *inout_seq = s_seq;
        unlock();
        return 0;
    }
    uint64_t oldest = s_seq - s_count;
    if (last < oldest) {
        last = oldest;
    }
    size_t n = (size_t)(s_seq - last);
    if (n > max_n) {
        n = max_n;
    }
    size_t phys = (s_head + s_cap - (size_t)(s_seq - last)) % s_cap;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_ring[(phys + i) % s_cap];
    }
    *inout_seq = last + n;
    unlock();
    return n;
}

void bus_capture_get_stats(bus_capture_stats_t *out)
{
    if (!out) {
        return;
    }
    lock();
    out->rx_count = s_rx_count;
    out->tx_count = s_tx_count;
    out->err_count = s_err_count;
    out->drop_count = s_drop_count;
    out->rx_rate = s_rx_rate;
    out->total_pushed = s_seq;
    out->ring_used = (uint32_t)s_count;
    out->ring_cap = (uint32_t)s_cap;
    unlock();
}

void bus_capture_get_ch_stats(bus_ch_stats_t out[BUS_SRC_COUNT])
{
    if (!out) {
        return;
    }
    lock();
    memcpy(out, s_ch, sizeof(s_ch));
    unlock();
}

size_t bus_capture_get_load_hist(uint8_t src, uint8_t *out, size_t max_n)
{
    if (!out || max_n == 0 || !src_ok(src)) {
        return 0;
    }
    lock();
    size_t n = s_hist_count < max_n ? s_hist_count : max_n;
    size_t start = (s_hist_head + BUS_RATE_HIST_LEN - n) % BUS_RATE_HIST_LEN;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_hist[src][(start + i) % BUS_RATE_HIST_LEN];
    }
    unlock();
    return n;
}

size_t bus_capture_get_top_ids(uint8_t src, bus_id_stat_t *out, size_t max_n)
{
    if (!out || max_n == 0 || !src_ok(src)) {
        return 0;
    }
    if (max_n > BUS_TOP_ID_N) {
        max_n = BUS_TOP_ID_N;
    }
    lock();
    bus_id_stat_t tmp[BUS_IDTAB_SLOTS];
    memcpy(tmp, s_idtab[src], sizeof(tmp));
    unlock();

    /* 简单选择排序取前 max_n */
    size_t got = 0;
    for (size_t k = 0; k < max_n; k++) {
        int best = -1;
        for (int i = 0; i < BUS_IDTAB_SLOTS; i++) {
            if (tmp[i].count == 0) {
                continue;
            }
            if (best < 0 || tmp[i].count > tmp[best].count) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        out[got++] = tmp[best];
        tmp[best].count = 0;
    }
    return got;
}

void bus_capture_clear(void)
{
    lock();
    if (s_ring && s_cap) {
        memset(s_ring, 0, s_cap * sizeof(bus_frame_t));
    }
    s_head = s_count = 0;
    s_seq = 0;
    s_rx_count = s_tx_count = s_err_count = s_drop_count = 0;
    s_rx_rate = 0;
    memset(s_ch_rx, 0, sizeof(s_ch_rx));
    memset(s_ch_tx, 0, sizeof(s_ch_tx));
    memset(s_ch_err, 0, sizeof(s_ch_err));
    memset(s_win_frames, 0, sizeof(s_win_frames));
    memset(s_win_bytes, 0, sizeof(s_win_bytes));
    memset(s_win_bits, 0, sizeof(s_win_bits));
    s_win_rx_all = 0;
    s_win_start_us = 0;
    memset(s_ch, 0, sizeof(s_ch));
    memset(s_hist, 0, sizeof(s_hist));
    s_hist_head = 0;
    s_hist_count = 0;
    s_hist_seq++;
    memset(s_idtab, 0, sizeof(s_idtab));
    unlock();
}
