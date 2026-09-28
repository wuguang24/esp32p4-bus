/**
 * @file bus_cli.c
 */

#include "bus_cli.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "bus_app.h"
#include "bus_i2c.h"
#include "bus_spi.h"
#include "bus_tools.h"
#include "siggen_pwm.h"
#include "bus_adc.h"
#include "bus_dio.h"
#include "bus_onewire.h"
#include "driver/gpio.h"

static const char *TAG = "bus_cli";

static int cli_casecmp(const char *a, const char *b)
{
    if (!a || !b) {
        return (a == b) ? 0 : 1;
    }
    while (*a && *b) {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb) {
            return ca - cb;
        }
    }
    return (unsigned char)*a - (unsigned char)*b;
}

#define strcasecmp cli_casecmp

static const char *s_help =
    "BUS CLI — commands:\n"
    "  help | status | clear\n"
    "  allow_tx on|off | listen on|off\n"
    "  can baud <n> | can auto | can send <id_hex> <hexbytes>\n"
    "  uart baud <n> | uart auto | uart send <hex>\n"
    "  rs485 baud <n> | rs485 auto | rs485 send <hex>\n"
    "  i2c hz <n> | i2c scan | i2c id | i2c dump <addr> <reg> [len] [aw=1|2]\n"
    "  spi hz <n> | spi mode <0-3> | spi jedec | spi read <addr> [len]\n"
    "  logger start|stop | sd remount\n"
    "  pwm on|off | pwm freq <hz> | pwm duty <0-100>\n"
    "  adc\n"
    "  dio mode in|out|pu|pd | dio read | dio write 0|1 | dio gpio <n>\n"
    "  ow scan | ow rom\n"
    "  cfg save | cfg reset\n";

const char *bus_cli_help(void)
{
    return s_help;
}

static void emit(bus_cli_out_fn out, void *user, const char *s)
{
    if (out) {
        out(s, user);
    } else {
        ESP_LOGI(TAG, "%s", s);
    }
}

static void emitf(bus_cli_out_fn out, void *user, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    emit(out, user, buf);
}

static int parse_hex_bytes(const char *s, uint8_t *out, int max_n)
{
    int n = 0;
    while (*s && n < max_n) {
        while (*s == ' ' || *s == ',' || *s == ':') {
            s++;
        }
        if (!*s) {
            break;
        }
        char *end = NULL;
        unsigned long v = strtoul(s, &end, 16);
        if (end == s) {
            break;
        }
        out[n++] = (uint8_t)(v & 0xFFu);
        s = end;
    }
    return n;
}

static bool parse_onoff(const char *s, bool *v)
{
    if (!s || !v) {
        return false;
    }
    if (!strcasecmp(s, "on") || !strcmp(s, "1") || !strcasecmp(s, "true")) {
        *v = true;
        return true;
    }
    if (!strcasecmp(s, "off") || !strcmp(s, "0") || !strcasecmp(s, "false")) {
        *v = false;
        return true;
    }
    return false;
}

static void hex_dump_line(bus_cli_out_fn out, void *user, const uint8_t *d, size_t n)
{
    char line[200];
    size_t p = 0;
    for (size_t i = 0; i < n && p + 4 < sizeof(line); i++) {
        p += (size_t)snprintf(line + p, sizeof(line) - p, "%02X%s", d[i],
                              (i + 1 < n) ? " " : "");
    }
    emit(out, user, line);
}

int bus_cli_exec(const char *line, bus_cli_out_fn out, void *user)
{
    if (!line) {
        return -1;
    }
    while (*line && isspace((unsigned char)*line)) {
        line++;
    }
    if (!*line || *line == '#') {
        return 0;
    }

    char buf[192];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    /* strip CR */
    for (char *p = buf; *p; p++) {
        if (*p == '\r' || *p == '\n') {
            *p = '\0';
            break;
        }
    }

    char *argv[12];
    int argc = 0;
    char *tok = strtok(buf, " \t");
    while (tok && argc < 12) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t");
    }
    if (argc == 0) {
        return 0;
    }

    if (!strcasecmp(argv[0], "help") || !strcmp(argv[0], "?")) {
        const char *p = s_help;
        while (*p) {
            char line[120];
            size_t i = 0;
            while (*p && *p != '\n' && i + 1 < sizeof(line)) {
                line[i++] = *p++;
            }
            line[i] = '\0';
            if (*p == '\n') {
                p++;
            }
            if (line[0]) {
                emit(out, user, line);
            }
        }
        return 0;
    }

    if (!strcasecmp(argv[0], "status")) {
        bus_app_status_t st;
        bus_app_get_status(&st);
        emitf(out, user, "can=%lu listen=%d allow_tx=%d",
              (unsigned long)st.can_baud, (int)st.listen_only, (int)st.allow_tx);
        emitf(out, user, "uart=%lu rs485=%lu i2c=%lu spi=%lu/%u",
              (unsigned long)st.uart_baud, (unsigned long)st.rs485_baud,
              (unsigned long)st.i2c_hz, (unsigned long)st.spi_hz, (unsigned)st.spi_mode);
        emitf(out, user, "ring=%u/%u drop=%lu sd=%d rec=%d",
              (unsigned)st.stats.ring_used, (unsigned)st.stats.ring_cap,
              (unsigned long)st.stats.drop_count, (int)st.sd_mounted, (int)st.recording);
        return 0;
    }

    if (!strcasecmp(argv[0], "clear")) {
        bus_app_clear_capture();
        emit(out, user, "OK clear");
        return 0;
    }

    if (!strcasecmp(argv[0], "allow_tx") && argc >= 2) {
        bool v;
        if (!parse_onoff(argv[1], &v)) {
            emit(out, user, "ERR allow_tx on|off");
            return -1;
        }
        esp_err_t e = bus_app_set_allow_tx(v);
        emitf(out, user, "%s allow_tx=%d", e == ESP_OK ? "OK" : "ERR", (int)v);
        return e == ESP_OK ? 0 : -1;
    }

    if (!strcasecmp(argv[0], "listen") && argc >= 2) {
        bool v;
        if (!parse_onoff(argv[1], &v)) {
            emit(out, user, "ERR listen on|off");
            return -1;
        }
        esp_err_t e = bus_app_set_listen(v);
        emitf(out, user, "%s listen=%d", e == ESP_OK ? "OK" : "ERR", (int)v);
        return e == ESP_OK ? 0 : -1;
    }

    if (!strcasecmp(argv[0], "can")) {
        if (argc >= 3 && !strcasecmp(argv[1], "baud")) {
            uint32_t b = (uint32_t)strtoul(argv[2], NULL, 0);
            esp_err_t e = bus_app_set_can_baud(b);
            emitf(out, user, "%s can baud %lu", e == ESP_OK ? "OK" : "ERR", (unsigned long)b);
            return e == ESP_OK ? 0 : -1;
        }
        if (argc >= 2 && !strcasecmp(argv[1], "auto")) {
            uint32_t b = 0;
            esp_err_t e = bus_tools_can_autodetect(&b, 180);
            emitf(out, user, "%s can auto -> %lu",
                  (e == ESP_OK || e == ESP_ERR_NOT_FOUND) ? "OK" : "ERR",
                  (unsigned long)b);
            return 0;
        }
        if (argc >= 3 && !strcasecmp(argv[1], "send")) {
            uint32_t id = (uint32_t)strtoul(argv[2], NULL, 16);
            uint8_t data[8];
            int n = 0;
            if (argc >= 4) {
                n = parse_hex_bytes(argv[3], data, 8);
            }
            esp_err_t e = bus_app_can_send(id, id > 0x7FFu, data, (size_t)n);
            emitf(out, user, "%s can send %lX %dB", e == ESP_OK ? "OK" : "ERR",
                  (unsigned long)id, n);
            return e == ESP_OK ? 0 : -1;
        }
        emit(out, user, "ERR can baud|auto|send");
        return -1;
    }

    if (!strcasecmp(argv[0], "uart")) {
        if (argc >= 2 && !strcasecmp(argv[1], "auto")) {
            uint32_t b = 0;
            esp_err_t e = bus_tools_uart_autodetect(&b, 120);
            emitf(out, user, "%s uart auto -> %lu",
                  (e == ESP_OK || e == ESP_ERR_NOT_FOUND) ? "OK" : "ERR",
                  (unsigned long)b);
            return 0;
        }
        if (argc >= 3 && !strcasecmp(argv[1], "baud")) {
            uint32_t b = (uint32_t)strtoul(argv[2], NULL, 0);
            esp_err_t e = bus_app_set_uart_baud(b);
            emitf(out, user, "%s uart baud %lu", e == ESP_OK ? "OK" : "ERR", (unsigned long)b);
            return e == ESP_OK ? 0 : -1;
        }
        if (argc >= 3 && !strcasecmp(argv[1], "send")) {
            uint8_t data[64];
            int n = parse_hex_bytes(argv[2], data, 64);
            esp_err_t e = bus_app_uart_send(data, (size_t)n);
            emitf(out, user, "%s uart send %dB", e == ESP_OK ? "OK" : "ERR", n);
            return e == ESP_OK ? 0 : -1;
        }
        emit(out, user, "ERR uart auto|baud|send");
        return -1;
    }

    if (!strcasecmp(argv[0], "rs485")) {
        if (argc >= 3 && !strcasecmp(argv[1], "baud")) {
            uint32_t b = (uint32_t)strtoul(argv[2], NULL, 0);
            esp_err_t e = bus_app_set_rs485_baud(b);
            emitf(out, user, "%s rs485 baud %lu", e == ESP_OK ? "OK" : "ERR", (unsigned long)b);
            return e == ESP_OK ? 0 : -1;
        }
        if (argc >= 2 && !strcasecmp(argv[1], "auto")) {
            uint32_t b = 0;
            bus_rs485_parity_t p = BUS_RS485_PARITY_NONE;
            esp_err_t e = bus_tools_rs485_autodetect(&b, &p, 100);
            emitf(out, user, "%s rs485 auto -> %lu p=%d",
                  (e == ESP_OK || e == ESP_ERR_NOT_FOUND) ? "OK" : "ERR",
                  (unsigned long)b, (int)p);
            return 0;
        }
        if (argc >= 3 && !strcasecmp(argv[1], "send")) {
            uint8_t data[64];
            int n = parse_hex_bytes(argv[2], data, 64);
            esp_err_t e = bus_app_rs485_send(data, (size_t)n);
            emitf(out, user, "%s rs485 send %dB", e == ESP_OK ? "OK" : "ERR", n);
            return e == ESP_OK ? 0 : -1;
        }
        emit(out, user, "ERR rs485 baud|auto|send");
        return -1;
    }

    if (!strcasecmp(argv[0], "i2c")) {
        if (argc >= 2 && !strcasecmp(argv[1], "scan")) {
            uint8_t found[32];
            int n = bus_i2c_scan(found, 32);
            emitf(out, user, "OK i2c scan %d", n);
            if (n > 0) {
                hex_dump_line(out, user, found, (size_t)n);
            }
            return 0;
        }
        if (argc >= 2 && !strcasecmp(argv[1], "id")) {
            char txt[200];
            int n = bus_tools_i2c_identify(txt, sizeof(txt));
            emit(out, user, txt);
            return n >= 0 ? 0 : -1;
        }
        if (argc >= 3 && !strcasecmp(argv[1], "hz")) {
            uint32_t hz = (uint32_t)strtoul(argv[2], NULL, 0);
            esp_err_t e = bus_app_set_i2c_hz(hz);
            emitf(out, user, "%s i2c hz %lu", e == ESP_OK ? "OK" : "ERR", (unsigned long)hz);
            return e == ESP_OK ? 0 : -1;
        }
        if (argc >= 4 && !strcasecmp(argv[1], "dump")) {
            uint8_t addr = (uint8_t)strtoul(argv[2], NULL, 16);
            uint16_t reg = (uint16_t)strtoul(argv[3], NULL, 16);
            size_t len = (argc >= 5) ? (size_t)strtoul(argv[4], NULL, 0) : 16;
            uint8_t aw = 1;
            if (argc >= 6) {
                aw = (uint8_t)strtoul(argv[5], NULL, 0);
            }
            if (len > 64) {
                len = 64;
            }
            uint8_t data[64];
            esp_err_t e = bus_tools_i2c_dump(addr, reg, aw, data, len);
            if (e != ESP_OK) {
                emitf(out, user, "ERR i2c dump %s", esp_err_to_name(e));
                return -1;
            }
            emitf(out, user, "OK i2c dump @%02X reg=%04X len=%u", addr, reg, (unsigned)len);
            hex_dump_line(out, user, data, len);
            return 0;
        }
        emit(out, user, "ERR i2c scan|id|hz|dump");
        return -1;
    }

    if (!strcasecmp(argv[0], "spi")) {
        if (argc >= 3 && !strcasecmp(argv[1], "hz")) {
            uint32_t hz = (uint32_t)strtoul(argv[2], NULL, 0);
            esp_err_t e = bus_app_set_spi_hz(hz);
            emitf(out, user, "%s spi hz %lu", e == ESP_OK ? "OK" : "ERR", (unsigned long)hz);
            return e == ESP_OK ? 0 : -1;
        }
        if (argc >= 3 && !strcasecmp(argv[1], "mode")) {
            uint8_t m = (uint8_t)strtoul(argv[2], NULL, 0);
            esp_err_t e = bus_app_set_spi_mode(m);
            emitf(out, user, "%s spi mode %u", e == ESP_OK ? "OK" : "ERR", (unsigned)m);
            return e == ESP_OK ? 0 : -1;
        }
        if (argc >= 2 && !strcasecmp(argv[1], "jedec")) {
            uint8_t id[3];
            char name[48];
            esp_err_t e = bus_tools_spi_jedec(id, name, sizeof(name));
            if (e != ESP_OK) {
                emitf(out, user, "ERR spi jedec %s", esp_err_to_name(e));
                return -1;
            }
            emitf(out, user, "OK %s", name);
            return 0;
        }
        if (argc >= 3 && !strcasecmp(argv[1], "read")) {
            uint32_t addr = (uint32_t)strtoul(argv[2], NULL, 16);
            size_t len = (argc >= 4) ? (size_t)strtoul(argv[3], NULL, 0) : 16;
            if (len > 64) {
                len = 64;
            }
            uint8_t data[64];
            esp_err_t e = bus_tools_spi_flash_read(addr, data, len);
            if (e != ESP_OK) {
                emitf(out, user, "ERR spi read %s", esp_err_to_name(e));
                return -1;
            }
            emitf(out, user, "OK spi read @%06lX %uB", (unsigned long)addr, (unsigned)len);
            hex_dump_line(out, user, data, len);
            return 0;
        }
        emit(out, user, "ERR spi hz|mode|jedec|read");
        return -1;
    }

    if (!strcasecmp(argv[0], "logger") && argc >= 2) {
        if (!strcasecmp(argv[1], "start")) {
            esp_err_t e = bus_app_logger_start();
            emitf(out, user, "%s logger start", e == ESP_OK ? "OK" : "ERR");
            return e == ESP_OK ? 0 : -1;
        }
        if (!strcasecmp(argv[1], "stop")) {
            (void)bus_app_logger_stop();
            emit(out, user, "OK logger stop");
            return 0;
        }
    }

    if (!strcasecmp(argv[0], "sd") && argc >= 2 && !strcasecmp(argv[1], "remount")) {
        esp_err_t e = bus_app_sd_remount();
        emitf(out, user, "%s sd remount", e == ESP_OK ? "OK" : "ERR");
        return e == ESP_OK ? 0 : -1;
    }

    if (!strcasecmp(argv[0], "pwm")) {
        if (argc >= 2 && (!strcasecmp(argv[1], "on") || !strcasecmp(argv[1], "off"))) {
            bool on = !strcasecmp(argv[1], "on");
            esp_err_t e = on ? siggen_pwm_start() : siggen_pwm_stop();
            emitf(out, user, "%s pwm %s", e == ESP_OK ? "OK" : "ERR", argv[1]);
            return e == ESP_OK ? 0 : -1;
        }
        if (argc >= 3 && !strcasecmp(argv[1], "freq")) {
            uint32_t f = (uint32_t)strtoul(argv[2], NULL, 0);
            esp_err_t e = siggen_pwm_set_freq(f);
            emitf(out, user, "%s pwm freq %lu", e == ESP_OK ? "OK" : "ERR", (unsigned long)f);
            return e == ESP_OK ? 0 : -1;
        }
        if (argc >= 3 && !strcasecmp(argv[1], "duty")) {
            uint8_t d = (uint8_t)strtoul(argv[2], NULL, 0);
            esp_err_t e = siggen_pwm_set_duty(d);
            emitf(out, user, "%s pwm duty %u", e == ESP_OK ? "OK" : "ERR", (unsigned)d);
            return e == ESP_OK ? 0 : -1;
        }
        emit(out, user, "ERR pwm on|off|freq|duty");
        return -1;
    }

    if (!strcasecmp(argv[0], "adc")) {
        bus_adc_sample_t s;
        esp_err_t e = bus_adc_read_avg(&s, 8);
        if (e != ESP_OK) {
            emitf(out, user, "ERR adc %s", esp_err_to_name(e));
            return -1;
        }
        emitf(out, user, "OK adc %d mV raw=%d", s.mv, s.raw);
        return 0;
    }

    if (!strcasecmp(argv[0], "dio")) {
        if (argc >= 3 && !strcasecmp(argv[1], "gpio")) {
            int g = (int)strtol(argv[2], NULL, 0);
            esp_err_t e = bus_dio_init((gpio_num_t)g);
            emitf(out, user, "%s dio gpio %d", e == ESP_OK ? "OK" : "ERR", g);
            return e == ESP_OK ? 0 : -1;
        }
        if (argc >= 3 && !strcasecmp(argv[1], "mode")) {
            bus_dio_mode_t m = BUS_DIO_IN;
            if (!strcasecmp(argv[2], "out")) {
                m = BUS_DIO_OUT;
            } else if (!strcasecmp(argv[2], "pu")) {
                m = BUS_DIO_IN_PU;
            } else if (!strcasecmp(argv[2], "pd")) {
                m = BUS_DIO_IN_PD;
            }
            esp_err_t e = bus_dio_set_mode(m);
            emitf(out, user, "%s dio mode %s gpio=%d", e == ESP_OK ? "OK" : "ERR",
                  argv[2], (int)bus_dio_get_gpio());
            return e == ESP_OK ? 0 : -1;
        }
        if (argc >= 2 && !strcasecmp(argv[1], "read")) {
            int v = bus_dio_read();
            emitf(out, user, "OK dio GPIO%d = %d", (int)bus_dio_get_gpio(), v);
            return 0;
        }
        if (argc >= 3 && !strcasecmp(argv[1], "write")) {
            bool lvl = (argv[2][0] != '0');
            esp_err_t e = bus_dio_write(lvl);
            emitf(out, user, "%s dio write %d", e == ESP_OK ? "OK" : "ERR", (int)lvl);
            return e == ESP_OK ? 0 : -1;
        }
        emit(out, user, "ERR dio mode|read|write|gpio");
        return -1;
    }

    if (!strcasecmp(argv[0], "ow")) {
        (void)bus_ow_init(bus_dio_get_gpio());
        if (argc >= 2 && !strcasecmp(argv[1], "rom")) {
            uint8_t rom[8];
            esp_err_t e = bus_ow_read_rom(rom);
            if (e != ESP_OK) {
                emitf(out, user, "ERR ow rom %s", esp_err_to_name(e));
                return -1;
            }
            emit(out, user, "OK ow rom");
            hex_dump_line(out, user, rom, 8);
            return 0;
        }
        if (argc >= 2 && !strcasecmp(argv[1], "scan")) {
            uint8_t roms[4][8];
            int n = bus_ow_search(roms, 4);
            emitf(out, user, "OK ow scan %d", n);
            for (int i = 0; i < n; i++) {
                hex_dump_line(out, user, roms[i], 8);
            }
            return 0;
        }
        emit(out, user, "ERR ow scan|rom");
        return -1;
    }

    if (!strcasecmp(argv[0], "cfg")) {
        if (argc >= 2 && !strcasecmp(argv[1], "save")) {
            esp_err_t e = bus_app_settings_save();
            emitf(out, user, "%s cfg save", e == ESP_OK ? "OK" : "ERR");
            return e == ESP_OK ? 0 : -1;
        }
        if (argc >= 2 && !strcasecmp(argv[1], "reset")) {
            esp_err_t e = bus_app_settings_reset();
            emitf(out, user, "%s cfg reset", e == ESP_OK ? "OK" : "ERR");
            return e == ESP_OK ? 0 : -1;
        }
        emit(out, user, "ERR cfg save|reset");
        return -1;
    }

    emitf(out, user, "ERR unknown: %s (help)", argv[0]);
    return -1;
}
