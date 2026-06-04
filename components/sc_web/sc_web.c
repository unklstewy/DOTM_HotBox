/*
 * sc_web.c — Web Management Portal HTTP Server
 */

#include "sc_web.h"
#include "sc_config.h"
#include "sc_network.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "cJSON.h"
#include "driver/ledc.h"

static const char *TAG = "sc_web";

/* ── Assembly Externs for Embedded Web Assets ────────────────────────────── */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

extern const uint8_t index_js_start[] asm("_binary_index_js_start");
extern const uint8_t index_js_end[]   asm("_binary_index_js_end");

extern const uint8_t index_css_start[] asm("_binary_index_css_start");
extern const uint8_t index_css_end[]   asm("_binary_index_css_end");

/* ── Server Handle ───────────────────────────────────────────────────────── */
static httpd_handle_t s_server = NULL;
static int s_backlight_brightness = 80;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit((int)a) && isxdigit((int)b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A'-10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A'-10);
            else b -= '0';
            *dst++ = 16*a+b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

/* ── Static File Handlers ────────────────────────────────────────────────── */

static esp_err_t static_html_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

static esp_err_t static_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)index_js_start, index_js_end - index_js_start);
    return ESP_OK;
}

static esp_err_t static_css_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)index_css_start, index_css_end - index_css_start);
    return ESP_OK;
}

static esp_err_t default_handler(httpd_req_t *req)
{
    if (strncmp(req->uri, "/api/", 5) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "API Endpoint Not Found");
        return ESP_FAIL;
    }
    return static_html_handler(req);
}

/* ── REST API Handlers ───────────────────────────────────────────────────── */

static esp_err_t get_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    char ip_str[32] = "192.168.4.1";
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(sc_network_is_ap() ? "WIFI_AP_DEF" : "WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
    }
    
    char ssid[32] = "SC_Terminal";
    if (sc_network_is_ap()) {
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        snprintf(ssid, sizeof(ssid), "SC_Terminal_%02X%02X", mac[4], mac[5]);
    } else {
        nvs_handle_t h;
        if (nvs_open("sc_config", NVS_READONLY, &h) == ESP_OK) {
            size_t len = sizeof(ssid);
            nvs_get_str(h, "ssid", ssid, &len);
            nvs_close(h);
        }
    }
    
    char json[384];
    snprintf(json, sizeof(json),
             "{\"online\":true,\"ip\":\"%s\",\"mode\":\"%s\",\"ssid\":\"%s\",\"uptime\":%lu,\"free_heap\":%lu,\"psram_free\":%lu}",
             ip_str,
             sc_network_is_ap() ? "AP" : "STA",
             ssid,
             (unsigned long)(esp_timer_get_time() / 1000000),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
             
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static esp_err_t get_config_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    const sc_terminal_config_t *cfg = sc_config_get();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ship_id", cfg->ship_id);
    cJSON_AddStringToObject(root, "console_id", cfg->console_id);
    cJSON_AddNumberToObject(root, "terminal_index", cfg->terminal_index);
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_config_handler(httpd_req_t *req)
{
    char buf[128];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request body");
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    sc_terminal_config_t new_cfg = *sc_config_get();
    
    cJSON *ship = cJSON_GetObjectItem(root, "ship_id");
    if (ship && ship->valuestring) {
        strlcpy(new_cfg.ship_id, ship->valuestring, sizeof(new_cfg.ship_id));
    }
    cJSON *console = cJSON_GetObjectItem(root, "console_id");
    if (console && console->valuestring) {
        strlcpy(new_cfg.console_id, console->valuestring, sizeof(new_cfg.console_id));
    }
    cJSON *index = cJSON_GetObjectItem(root, "terminal_index");
    if (index) {
        new_cfg.terminal_index = index->valueint;
    }
    
    cJSON_Delete(root);
    
    esp_err_t ret = sc_config_save(&new_cfg);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save configuration");
        return ESP_FAIL;
    }
    
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t get_wifi_scan_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    
    // Allocate buffer on stack/heap for network scan
    char *scan_buf = malloc(2048);
    if (!scan_buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    esp_err_t err = sc_network_scan_wifi(scan_buf, 2048);
    if (err != ESP_OK) {
        free(scan_buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi Scan Failed");
        return ESP_FAIL;
    }
    
    httpd_resp_sendstr(req, scan_buf);
    free(scan_buf);
    return ESP_OK;
}

static esp_err_t post_wifi_config_handler(httpd_req_t *req)
{
    char buf[128];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request body");
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass = cJSON_GetObjectItem(root, "password");
    if (!ssid || !ssid->valuestring) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }
    
    const char *wifi_ssid = ssid->valuestring;
    const char *wifi_pass = (pass && pass->valuestring) ? pass->valuestring : "";
    
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    
    // Let the response finish sending before setting credentials (which reboots the board)
    vTaskDelay(pdMS_TO_TICKS(100));
    
    sc_network_set_wifi_credentials(wifi_ssid, wifi_pass);
    
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t get_fs_list_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    
    char query[256] = {0};
    char req_path[256] = {0};
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[256];
        if (httpd_query_key_value(query, "path", param, sizeof(param)) == ESP_OK) {
            url_decode(req_path, param);
        }
    }
    
    char fullpath[512];
    snprintf(fullpath, sizeof(fullpath), "/sdcard%s", req_path);
    
    // Check trailing slash
    size_t f_len = strlen(fullpath);
    if (f_len > 1 && fullpath[f_len - 1] == '/') {
        fullpath[f_len - 1] = '\0';
    }
    
    DIR *dir = opendir(fullpath);
    if (!dir) {
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    
    cJSON *arr = cJSON_CreateArray();
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", fullpath, entry->d_name);
        
        struct stat st;
        stat(filepath, &st);
        
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", entry->d_name);
        cJSON_AddBoolToObject(item, "is_dir", S_ISDIR(st.st_mode));
        cJSON_AddNumberToObject(item, "size", st.st_size);
        
        cJSON_AddItemToArray(arr, item);
    }
    closedir(dir);
    
    char *json = cJSON_PrintUnformatted(arr);
    httpd_resp_sendstr(req, json);
    
    free(json);
    cJSON_Delete(arr);
    return ESP_OK;
}

static esp_err_t get_fs_read_handler(httpd_req_t *req)
{
    char query[256] = {0};
    char req_path[256] = {0};
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[256];
        if (httpd_query_key_value(query, "path", param, sizeof(param)) == ESP_OK) {
            url_decode(req_path, param);
        }
    }
    
    char fullpath[512];
    snprintf(fullpath, sizeof(fullpath), "/sdcard%s", req_path);
    
    FILE *f = fopen(fullpath, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }
    
    if (strstr(fullpath, ".json")) {
        httpd_resp_set_type(req, "application/json");
    } else {
        httpd_resp_set_type(req, "application/octet-stream");
    }
    
    char *buf = malloc(4096);
    if (!buf) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, 4096, f)) > 0) {
        httpd_resp_send_chunk(req, buf, read_bytes);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    
    fclose(f);
    free(buf);
    return ESP_OK;
}

static esp_err_t post_fs_upload_handler(httpd_req_t *req)
{
    char target_path[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-File-Path", target_path, sizeof(target_path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing X-File-Path header");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Uploading file to path: %s", target_path);
    
    FILE *f = fopen(target_path, "wb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open target file for writing");
        return ESP_FAIL;
    }
    
    char *buf = malloc(4096);
    if (!buf) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    int received = 0;
    int remaining = req->content_len;
    
    while (remaining > 0) {
        int to_receive = (remaining > 4096) ? 4096 : remaining;
        received = httpd_req_recv(req, buf, to_receive);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            fclose(f);
            free(buf);
            unlink(target_path);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Socket read error");
            return ESP_FAIL;
        }
        fwrite(buf, 1, received, f);
        remaining -= received;
    }
    
    fclose(f);
    free(buf);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t post_fs_delete_handler(httpd_req_t *req)
{
    char buf[256];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request body");
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *path = cJSON_GetObjectItem(root, "path");
    if (!path || !path->valuestring) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing path");
        return ESP_FAIL;
    }
    
    int ret = unlink(path->valuestring);
    cJSON_Delete(root);
    
    if (ret != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete file");
        return ESP_FAIL;
    }
    
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t post_system_reboot_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t get_system_backlight_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    char json[64];
    snprintf(json, sizeof(json), "{\"brightness\":%d}", s_backlight_brightness);
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

static esp_err_t post_system_backlight_handler(httpd_req_t *req)
{
    char buf[64];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
    if (brightness) {
        int val = brightness->valueint;
        if (val < 5) val = 5;
        if (val > 100) val = 100;
        
        s_backlight_brightness = val;
        int duty = (val * 1023) / 100;
        
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        
        ESP_LOGI(TAG, "Backlight set to %d%% (LEDC duty %d)", val, duty);
    }
    
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

esp_err_t sc_web_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.stack_size = 8192; // Boost stack size for file/JSON work

    ESP_LOGI(TAG, "Starting Web Portal server on port %d...", config.server_port);
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start httpd server: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── Static URIs ─────────────────────────────────────────────────────── */
    httpd_uri_t html_uri = { .uri = "/", .method = HTTP_GET, .handler = static_html_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &html_uri);
    
    httpd_uri_t js_uri = { .uri = "/assets/index.js", .method = HTTP_GET, .handler = static_js_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &js_uri);
    
    httpd_uri_t css_uri = { .uri = "/assets/index.css", .method = HTTP_GET, .handler = static_css_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &css_uri);

    /* ── REST APIs ───────────────────────────────────────────────────────── */
    httpd_uri_t api_status = { .uri = "/api/status", .method = HTTP_GET, .handler = get_status_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &api_status);

    httpd_uri_t api_config_get = { .uri = "/api/config", .method = HTTP_GET, .handler = get_config_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &api_config_get);

    httpd_uri_t api_config_post = { .uri = "/api/config", .method = HTTP_POST, .handler = post_config_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &api_config_post);

    httpd_uri_t api_wifi_scan = { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = get_wifi_scan_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &api_wifi_scan);

    httpd_uri_t api_wifi_config = { .uri = "/api/wifi/config", .method = HTTP_POST, .handler = post_wifi_config_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &api_wifi_config);

    httpd_uri_t api_fs_list = { .uri = "/api/fs/list", .method = HTTP_GET, .handler = get_fs_list_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &api_fs_list);

    httpd_uri_t api_fs_read = { .uri = "/api/fs/read", .method = HTTP_GET, .handler = get_fs_read_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &api_fs_read);

    httpd_uri_t api_fs_upload = { .uri = "/api/fs/upload", .method = HTTP_POST, .handler = post_fs_upload_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &api_fs_upload);

    httpd_uri_t api_fs_delete = { .uri = "/api/fs/delete", .method = HTTP_POST, .handler = post_fs_delete_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &api_fs_delete);

    httpd_uri_t api_reboot = { .uri = "/api/system/reboot", .method = HTTP_POST, .handler = post_system_reboot_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &api_reboot);

    httpd_uri_t api_backlight_get = { .uri = "/api/system/backlight", .method = HTTP_GET, .handler = get_system_backlight_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &api_backlight_get);

    httpd_uri_t api_backlight_post = { .uri = "/api/system/backlight", .method = HTTP_POST, .handler = post_system_backlight_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &api_backlight_post);

    /* ── Catch-All ───────────────────────────────────────────────────────── */
    httpd_uri_t catch_all = { .uri = "/*", .method = HTTP_GET, .handler = default_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &catch_all);

    return ESP_OK;
}

void sc_web_stop(void)
{
    if (s_server != NULL) {
        ESP_LOGI(TAG, "Stopping Web Portal server...");
        httpd_stop(s_server);
        s_server = NULL;
    }
}
