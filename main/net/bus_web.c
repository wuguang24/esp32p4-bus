/**
 * @file bus_web.c
 * @brief 轻量 HTTP：CLI + SD 日志列表/下载
 */

#include "bus_web.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "bus_cli.h"
#include "sdmmc.h"

static const char *TAG = "bus_web";
static httpd_handle_t s_server;
static SemaphoreHandle_t s_cli_mu;

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} cli_acc_t;

static void cli_acc_out(const char *line, void *user)
{
    cli_acc_t *a = (cli_acc_t *)user;
    if (!a || !a->buf || !line) {
        return;
    }
    size_t n = strlen(line);
    if (a->len + n + 2 >= a->cap) {
        return;
    }
    memcpy(a->buf + a->len, line, n);
    a->len += n;
    a->buf[a->len++] = '\n';
    a->buf[a->len] = '\0';
}

static const char s_index_html[] =
    "<!DOCTYPE html><html><head><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>ESP32P4 BUS CLI</title>"
    "<style>"
    "body{margin:0;background:#05070A;color:#D8E6F0;font:14px/1.4 monospace}"
    "h1{font:600 16px sans-serif;color:#2EE8C8;margin:12px 16px}"
    "#log{height:55vh;overflow:auto;padding:12px;background:#030507;border:1px solid #243040;margin:0 12px;white-space:pre-wrap}"
    "form{display:flex;gap:8px;padding:12px}"
    "input{flex:1;background:#121A22;color:#D8E6F0;border:1px solid #3A5060;padding:10px;border-radius:4px}"
    "button{background:#0E6B5C;color:#fff;border:1px solid #2EE8C8;padding:10px 16px;border-radius:4px}"
    ".hint{color:#7A8B9A;margin:0 16px 8px;font:12px sans-serif}"
    "a{color:#4AA3FF}"
    "#files{margin:0 16px 12px;font:12px sans-serif}"
    "</style></head><body>"
    "<h1>ESP32P4 · BUS CLI</h1>"
    "<p class=hint>http://192.168.4.1 — help / uart auto / i2c id / spi jedec / dio read / ow scan</p>"
    "<div id=files></div>"
    "<div id=log></div>"
    "<form id=f><input id=cmd autocomplete=off placeholder=\"help / status / uart auto …\">"
    "<button type=submit>发送</button></form>"
    "<script>"
    "const log=document.getElementById('log');"
    "function add(t){log.textContent+=t;log.scrollTop=log.scrollHeight}"
    "async function loadFiles(){"
    "try{const r=await fetch('/api/logs');const t=await r.text();"
    "document.getElementById('files').innerHTML=t||'<span class=hint>无 SD 日志</span>'}catch(e){}}"
    "document.getElementById('f').onsubmit=async(e)=>{"
    "e.preventDefault();const c=document.getElementById('cmd');const v=c.value.trim();if(!v)return;"
    "add('> '+v+'\\n');c.value='';"
    "try{const r=await fetch('/api/cli',{method:'POST',headers:{'Content-Type':'text/plain'},body:v});"
    "add(await r.text())}catch(err){add('ERR '+err+'\\n')}"
    "};add('Ready. Type help\\n');loadFiles();"
    "</script></body></html>";

static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, s_index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_cli(httpd_req_t *req)
{
    char body[192];
    int total = req->content_len;
    if (total <= 0 || total >= (int)sizeof(body)) {
        total = (total < 0) ? 0 : (int)sizeof(body) - 1;
    }
    int r = httpd_req_recv(req, body, total);
    if (r <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
        return ESP_FAIL;
    }
    body[r] = '\0';

    char out[1024];
    cli_acc_t acc = {.buf = out, .cap = sizeof(out), .len = 0};
    out[0] = '\0';

    if (s_cli_mu) {
        xSemaphoreTake(s_cli_mu, portMAX_DELAY);
    }
    (void)bus_cli_exec(body, cli_acc_out, &acc);
    if (s_cli_mu) {
        xSemaphoreGive(s_cli_mu);
    }

    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_logs(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    if (sdmmc_mount_flag != 0x01) {
        return httpd_resp_sendstr(req, "<span class=hint>SD 未挂载</span>");
    }
    DIR *d = opendir(MOUNT_POINT);
    if (!d) {
        return httpd_resp_sendstr(req, "<span class=hint>无法打开 /sdcard</span>");
    }
    httpd_resp_sendstr_chunk(req, "<b>SD 日志</b><br>");
    struct dirent *de;
    int n = 0;
    while ((de = readdir(d)) != NULL && n < 32) {
        if (de->d_name[0] == '.') {
            continue;
        }
        const char *name = de->d_name;
        size_t len = strlen(name);
        if (len < 5 || strcmp(name + len - 4, ".csv") != 0) {
            continue;
        }
        char line[160];
        snprintf(line, sizeof(line),
                 "<a href=\"/files/%s\">%s</a><br>", name, name);
        httpd_resp_sendstr_chunk(req, line);
        n++;
    }
    closedir(d);
    if (n == 0) {
        httpd_resp_sendstr_chunk(req, "<span class=hint>暂无 csv</span>");
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t handler_file(httpd_req_t *req)
{
    const char *uri = req->uri;
    const char *fname = strrchr(uri, '/');
    if (!fname || !fname[1]) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no file");
        return ESP_FAIL;
    }
    fname++;
    if (strchr(fname, '/') || strstr(fname, "..")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad name");
        return ESP_FAIL;
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, fname);
    FILE *f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "missing");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/csv");
    char disp[96];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", fname);
    httpd_resp_set_hdr(req, "Content-Disposition", disp);

    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t bus_web_start(void)
{
    if (s_server) {
        return ESP_OK;
    }
    if (!s_cli_mu) {
        s_cli_mu = xSemaphoreCreateMutex();
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 8;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = handler_root};
    const httpd_uri_t cli = {.uri = "/api/cli", .method = HTTP_POST, .handler = handler_cli};
    const httpd_uri_t logs = {.uri = "/api/logs", .method = HTTP_GET, .handler = handler_logs};
    const httpd_uri_t files = {.uri = "/files/*", .method = HTTP_GET, .handler = handler_file};
    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &cli);
    httpd_register_uri_handler(s_server, &logs);
    httpd_register_uri_handler(s_server, &files);
    ESP_LOGI(TAG, "HTTP CLI+logs http://192.168.4.1/");
    return ESP_OK;
}

void bus_web_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

bool bus_web_is_running(void)
{
    return s_server != NULL;
}
