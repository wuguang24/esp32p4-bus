#include "bus_framer.h"

#include <string.h>

void bus_framer_init(bus_framer_t *fr, const bus_framer_cfg_t *cfg)
{
    if (!fr) {
        return;
    }
    memset(fr, 0, sizeof(*fr));
    if (cfg) {
        fr->cfg = *cfg;
    } else {
        fr->cfg.mode = BUS_FR_IDLE;
        fr->cfg.fixed_len = 8;
    }
    if (fr->cfg.fixed_len < 1) {
        fr->cfg.fixed_len = 1;
    }
    if (fr->cfg.fixed_len > BUS_UART_FRAME_MAX) {
        fr->cfg.fixed_len = BUS_UART_FRAME_MAX;
    }
    if (fr->cfg.sof_n > 4) {
        fr->cfg.sof_n = 4;
    }
    if (fr->cfg.eof_n > 4) {
        fr->cfg.eof_n = 4;
    }
}

void bus_framer_reset(bus_framer_t *fr)
{
    if (!fr) {
        return;
    }
    fr->len = 0;
    fr->in_frame = false;
    fr->sof_match = 0;
}

void bus_framer_set_cfg(bus_framer_t *fr, const bus_framer_cfg_t *cfg)
{
    if (!fr || !cfg) {
        return;
    }
    bus_framer_cfg_t c = *cfg;
    if (c.fixed_len < 1) {
        c.fixed_len = 1;
    }
    if (c.fixed_len > BUS_UART_FRAME_MAX) {
        c.fixed_len = BUS_UART_FRAME_MAX;
    }
    if (c.sof_n > 4) {
        c.sof_n = 4;
    }
    if (c.eof_n > 4) {
        c.eof_n = 4;
    }
    fr->cfg = c;
    bus_framer_reset(fr);
}

void bus_framer_get_cfg(const bus_framer_t *fr, bus_framer_cfg_t *out)
{
    if (fr && out) {
        *out = fr->cfg;
    }
}

static void emit_frame(bus_framer_t *fr, bus_framer_emit_fn emit, void *user)
{
    if (!emit || fr->len == 0) {
        return;
    }
    emit(fr->buf, fr->len, user);
    fr->len = 0;
    fr->in_frame = false;
    fr->sof_match = 0;
}

static bool ends_with_eof(const bus_framer_t *fr)
{
    if (fr->cfg.eof_n == 0 || fr->len < fr->cfg.eof_n) {
        return false;
    }
    return memcmp(fr->buf + fr->len - fr->cfg.eof_n, fr->cfg.eof, fr->cfg.eof_n) == 0;
}

void bus_framer_feed(bus_framer_t *fr, const uint8_t *data, size_t n,
                     bus_framer_emit_fn emit, void *user)
{
    if (!fr || !data || n == 0 || !emit) {
        return;
    }

    if (fr->cfg.mode == BUS_FR_IDLE) {
        emit(data, n > BUS_UART_FRAME_MAX ? BUS_UART_FRAME_MAX : n, user);
        return;
    }

    for (size_t i = 0; i < n; i++) {
        const uint8_t b = data[i];

        if (fr->cfg.mode == BUS_FR_FIXED) {
            fr->buf[fr->len++] = b;
            if (fr->len >= fr->cfg.fixed_len) {
                emit_frame(fr, emit, user);
            }
            continue;
        }

        /* BUS_FR_MARK */
        if (!fr->in_frame) {
            if (fr->cfg.sof_n == 0) {
                fr->in_frame = true;
                fr->buf[0] = b;
                fr->len = 1;
            } else if (b == fr->cfg.sof[fr->sof_match]) {
                fr->sof_match++;
                if (fr->sof_match >= fr->cfg.sof_n) {
                    memcpy(fr->buf, fr->cfg.sof, fr->cfg.sof_n);
                    fr->len = fr->cfg.sof_n;
                    fr->in_frame = true;
                    fr->sof_match = 0;
                }
            } else {
                fr->sof_match = (b == fr->cfg.sof[0]) ? 1 : 0;
            }
            continue;
        }

        if (fr->len < BUS_UART_FRAME_MAX) {
            fr->buf[fr->len++] = b;
        }

        if (fr->cfg.eof_n > 0 && ends_with_eof(fr)) {
            emit_frame(fr, emit, user);
            continue;
        }

        /* 无帧尾：定长收满或缓冲满则出帧 */
        if (fr->cfg.eof_n == 0 && fr->len >= fr->cfg.fixed_len) {
            emit_frame(fr, emit, user);
            continue;
        }
        if (fr->len >= BUS_UART_FRAME_MAX) {
            emit_frame(fr, emit, user);
        }
    }
}
