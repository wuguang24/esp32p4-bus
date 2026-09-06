/**
 * @file bus_decode.c
 */

#include "bus_decode.h"

#include <stdio.h>
#include <string.h>

static void append_hex(char *out, size_t out_sz, size_t *pos, const uint8_t *data, size_t n, size_t max_show)
{
    size_t show = n < max_show ? n : max_show;
    for (size_t i = 0; i < show && *pos + 3 < out_sz; i++) {
        *pos += (size_t)snprintf(out + *pos, out_sz - *pos, "%02X", data[i]);
        if (i + 1 < show && *pos + 1 < out_sz) {
            out[(*pos)++] = ' ';
            out[*pos] = '\0';
        }
    }
    if (n > max_show && *pos + 4 < out_sz) {
        *pos += (size_t)snprintf(out + *pos, out_sz - *pos, "…");
    }
}

static uint16_t modbus_crc(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

const char *bus_decode_modbus_fc_name(uint8_t fc)
{
    switch (fc) {
    case 0x01: return "读线圈";
    case 0x02: return "读离散";
    case 0x03: return "读保持";
    case 0x04: return "读输入";
    case 0x05: return "写线圈";
    case 0x06: return "写寄存器";
    case 0x0F: return "写多线圈";
    case 0x10: return "写多寄存器";
    case 0x11: return "报告从站";
    case 0x16: return "掩码写";
    case 0x17: return "读写寄存器";
    default:   return NULL;
    }
}

void bus_decode_annotate(bus_frame_t *f)
{
    if (!f || f->src != BUS_SRC_RS485 || f->len < 4) {
        return;
    }
    uint16_t got = (uint16_t)f->data[f->len - 2] | ((uint16_t)f->data[f->len - 1] << 8);
    uint16_t calc = modbus_crc(f->data, (size_t)f->len - 2);
    f->flags &= (uint8_t)~(BUS_FLAG_CRC_OK | BUS_FLAG_CRC_BAD);
    if (got == calc) {
        f->flags |= BUS_FLAG_CRC_OK;
    } else {
        f->flags |= BUS_FLAG_CRC_BAD;
    }
}

void bus_decode_format_line(const bus_frame_t *f, uint64_t prev_us, char *out, size_t out_sz)
{
    if (!f || !out || out_sz == 0) {
        return;
    }
    out[0] = '\0';
    size_t pos = 0;
    const char *tag = "?";
    switch (f->src) {
    case BUS_SRC_CAN: tag = "CAN"; break;
    case BUS_SRC_RS485: tag = "485"; break;
    case BUS_SRC_UART: tag = "UART"; break;
    case BUS_SRC_I2C: tag = "I2C"; break;
    case BUS_SRC_SPI: tag = "SPI"; break;
    default: break;
    }
    char dir = f->dir ? 'T' : 'R';

    if (prev_us != 0 && f->t_us >= prev_us) {
        uint32_t dus = (uint32_t)(f->t_us - prev_us);
        if (dus > 999999u) {
            dus = 999999u;
        }
        pos += (size_t)snprintf(out + pos, out_sz - pos, "+%lu ", (unsigned long)dus);
    } else {
        uint32_t us = (uint32_t)(f->t_us % 1000000ULL);
        uint32_t sec = (uint32_t)(f->t_us / 1000000ULL);
        pos += (size_t)snprintf(out + pos, out_sz - pos, "%lu.%06lu ",
                                (unsigned long)sec, (unsigned long)us);
    }

    if (f->src == BUS_SRC_CAN) {
        pos += (size_t)snprintf(out + pos, out_sz - pos, "%s%c %s%lX [%u] ",
                                tag, dir,
                                (f->flags & BUS_FLAG_EXT) ? "E" : "",
                                (unsigned long)f->id, (unsigned)f->len);
        append_hex(out, out_sz, &pos, f->data, f->len, 8);
        if (f->flags & BUS_FLAG_RTR) {
            snprintf(out + pos, out_sz - pos, " RTR");
        }
    } else if (f->src == BUS_SRC_RS485) {
        pos += (size_t)snprintf(out + pos, out_sz - pos, "%s%c @%02lX ",
                                tag, dir, (unsigned long)(f->id & 0xFFu));
        if (f->len >= 2) {
            const char *fcn = bus_decode_modbus_fc_name(f->data[1]);
            if (fcn && (f->flags & BUS_FLAG_CRC_OK)) {
                pos += (size_t)snprintf(out + pos, out_sz - pos, "%s ", fcn);
            } else {
                pos += (size_t)snprintf(out + pos, out_sz - pos, "FC%02X ",
                                        (unsigned)f->data[1]);
            }
        }
        pos += (size_t)snprintf(out + pos, out_sz - pos, "L%u ", (unsigned)f->len);
        append_hex(out, out_sz, &pos, f->data, f->len, 10);
        if (f->flags & BUS_FLAG_CRC_OK) {
            snprintf(out + strlen(out), out_sz - strlen(out), " CRC");
        } else if (f->flags & BUS_FLAG_CRC_BAD) {
            snprintf(out + strlen(out), out_sz - strlen(out), " !CRC");
        }
    } else if (f->src == BUS_SRC_I2C) {
        pos += (size_t)snprintf(out + pos, out_sz - pos, "%s%c @%02lX L%u ",
                                tag, dir, (unsigned long)f->id, (unsigned)f->len);
        append_hex(out, out_sz, &pos, f->data, f->len, 12);
    } else {
        pos += (size_t)snprintf(out + pos, out_sz - pos, "%s%c L%u ",
                                tag, dir, (unsigned)f->len);
        append_hex(out, out_sz, &pos, f->data, f->len, 16);
    }
    if (f->flags & BUS_FLAG_ERR) {
        snprintf(out + strlen(out), out_sz - strlen(out), " !ERR");
    }
}

void bus_decode_format_detail(const bus_frame_t *f, char *out, size_t out_sz)
{
    if (!f || !out || out_sz == 0) {
        return;
    }
    out[0] = '\0';
    size_t pos = 0;
    pos += (size_t)snprintf(out + pos, out_sz - pos,
                            "t=%llu us  %s\n",
                            (unsigned long long)f->t_us,
                            f->dir ? "TX" : "RX");

    if (f->src == BUS_SRC_CAN) {
        pos += (size_t)snprintf(out + pos, out_sz - pos,
                                "CAN %s ID=0x%lX (%lu) DLC=%u%s%s\ndata: ",
                                (f->flags & BUS_FLAG_EXT) ? "扩展" : "标准",
                                (unsigned long)f->id, (unsigned long)f->id,
                                (unsigned)f->dlc,
                                (f->flags & BUS_FLAG_RTR) ? " RTR" : "",
                                (f->flags & BUS_FLAG_ERR) ? " ERR" : "");
        append_hex(out, out_sz, &pos, f->data, f->len, 8);
        return;
    }

    if (f->src == BUS_SRC_UART || f->src == BUS_SRC_RS485) {
        if (f->src == BUS_SRC_RS485) {
            pos += (size_t)snprintf(out + pos, out_sz - pos, "站号=%u len=%u\n",
                                    (unsigned)(f->id & 0xFFu), (unsigned)f->len);
        } else {
            pos += (size_t)snprintf(out + pos, out_sz - pos, "len=%u\n", (unsigned)f->len);
        }
        pos += (size_t)snprintf(out + pos, out_sz - pos, "hex: ");
        append_hex(out, out_sz, &pos, f->data, f->len, 32);
        pos += (size_t)snprintf(out + pos, out_sz - pos, "\nascii: ");
        for (size_t i = 0; i < f->len && pos + 2 < out_sz; i++) {
            char c = (char)f->data[i];
            out[pos++] = (c >= 32 && c < 127) ? c : '.';
        }
        out[pos] = '\0';

        if (f->src == BUS_SRC_RS485 && f->len >= 2) {
            uint8_t fc = f->data[1];
            const char *fcn = bus_decode_modbus_fc_name(fc);
            pos += (size_t)snprintf(out + pos, out_sz - pos,
                                    "\nModbus FC=0x%02X %s",
                                    (unsigned)fc, fcn ? fcn : "未知");
            if (f->len >= 4) {
                uint16_t got = (uint16_t)f->data[f->len - 2] | ((uint16_t)f->data[f->len - 1] << 8);
                uint16_t calc = modbus_crc(f->data, f->len - 2);
                pos += (size_t)snprintf(out + pos, out_sz - pos, " CRC=%04X %s",
                                        (unsigned)got, (got == calc) ? "OK" : "BAD");
                if (f->len >= 6 && (fc == 0x03 || fc == 0x04 || fc == 0x06 || fc == 0x10)) {
                    uint16_t reg = ((uint16_t)f->data[2] << 8) | f->data[3];
                    uint16_t qty = ((uint16_t)f->data[4] << 8) | f->data[5];
                    pos += (size_t)snprintf(out + pos, out_sz - pos,
                                            "\n寄存器=0x%04X 数量/值=%u",
                                            (unsigned)reg, (unsigned)qty);
                }
            }
        }
        return;
    }

    pos += (size_t)snprintf(out + pos, out_sz - pos, "id=0x%lX len=%u\nhex: ",
                            (unsigned long)f->id, (unsigned)f->len);
    append_hex(out, out_sz, &pos, f->data, f->len, 32);
}
