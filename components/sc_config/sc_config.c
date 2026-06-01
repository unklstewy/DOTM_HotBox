/*
 * sc_config.c — Ship & terminal configuration
 *
 * Mounts SPIFFS, reads NVS terminal identity, and exposes the merged config.
 * Wi-Fi credentials (SSID + PSK) are kept in NVS only — never in source.
 */

#include "sc_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_spiffs.h"
#include "esp_log.h"

static const char *TAG = "sc_config";

/* ── NVS namespace ──────────────────────────────────────────────────────── */
#define NVS_NS               "sc_config"
#define NVS_KEY_SHIP_ID      "ship_id"
#define NVS_KEY_CONSOLE_ID   "console_id"
#define NVS_KEY_TERM_IDX     "term_idx"
#define NVS_KEY_BRIDGE_HOST  "bridge_host"
#define NVS_KEY_BRIDGE_PORT  "bridge_port"
#define NVS_KEY_HID_EN       "hid_enabled"

/* ── Defaults ────────────────────────────────────────────────────────────── */
#define DEFAULT_SHIP_ID      "cutlass_black"
#define DEFAULT_CONSOLE_ID   "pilot_mfd_left"
#define DEFAULT_TERM_IDX     (0)
#define DEFAULT_BRIDGE_HOST  "sc-bridge.local"
#define DEFAULT_BRIDGE_PORT  (8765)
#define DEFAULT_HID_ENABLED  (true)

/* ── State ───────────────────────────────────────────────────────────────── */
static sc_terminal_config_t s_cfg;
static SemaphoreHandle_t    s_mutex;
static bool                 s_initialised = false;

/* ── SPIFFS config ────────────────────────────────────────────────────────── */
static const esp_vfs_spiffs_conf_t s_spiffs_conf = {
    .base_path              = "/spiffs",
    .partition_label        = "storage",
    .max_files              = 8,
    .format_if_mount_failed = true,
};

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void config_set_defaults(sc_terminal_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strlcpy(cfg->ship_id,      DEFAULT_SHIP_ID,    sizeof(cfg->ship_id));
    strlcpy(cfg->console_id,   DEFAULT_CONSOLE_ID, sizeof(cfg->console_id));
    cfg->terminal_index = DEFAULT_TERM_IDX;
    strlcpy(cfg->bridge_host,  DEFAULT_BRIDGE_HOST, sizeof(cfg->bridge_host));
    cfg->bridge_port   = DEFAULT_BRIDGE_PORT;
    cfg->hid_enabled   = DEFAULT_HID_ENABLED;
}

static esp_err_t nvs_load(sc_terminal_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS namespace not found — using defaults");
        config_set_defaults(cfg);
        return ESP_OK;
    }
    ESP_ERROR_CHECK(ret);

    size_t len;
    len = sizeof(cfg->ship_id);
    nvs_get_str(h, NVS_KEY_SHIP_ID, cfg->ship_id, &len);
    len = sizeof(cfg->console_id);
    nvs_get_str(h, NVS_KEY_CONSOLE_ID, cfg->console_id, &len);
    len = sizeof(cfg->bridge_host);
    nvs_get_str(h, NVS_KEY_BRIDGE_HOST, cfg->bridge_host, &len);

    uint8_t  u8;
    uint16_t u16;
    uint8_t  hid;
    if (nvs_get_u8(h,  NVS_KEY_TERM_IDX,    &u8)  == ESP_OK) cfg->terminal_index = u8;
    if (nvs_get_u16(h, NVS_KEY_BRIDGE_PORT, &u16) == ESP_OK) cfg->bridge_port    = u16;
    if (nvs_get_u8(h,  NVS_KEY_HID_EN,      &hid) == ESP_OK) cfg->hid_enabled    = (bool)hid;

    nvs_close(h);
    return ESP_OK;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t sc_config_init(void)
{
    if (s_initialised) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    /* Mount SPIFFS */
    esp_err_t ret = esp_vfs_spiffs_register(&s_spiffs_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPIFFS mounted at /spiffs");

    /* Load terminal identity from NVS */
    config_set_defaults(&s_cfg);
    ret = nvs_load(&s_cfg);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Config loaded: ship=%s console=%s terminal=%d",
             s_cfg.ship_id, s_cfg.console_id, s_cfg.terminal_index);

    s_initialised = true;
    return ESP_OK;
}

void sc_config_deinit(void)
{
    if (!s_initialised) return;
    esp_vfs_spiffs_unregister(s_spiffs_conf.partition_label);
    vSemaphoreDelete(s_mutex);
    s_initialised = false;
}

const sc_terminal_config_t *sc_config_get(void)
{
    return &s_cfg;   /* Read-only; mutations go through sc_config_save() */
}

esp_err_t sc_config_save(const sc_terminal_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_cfg = *cfg;
    xSemaphoreGive(s_mutex);

    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &h));
    nvs_set_str(h, NVS_KEY_SHIP_ID,     cfg->ship_id);
    nvs_set_str(h, NVS_KEY_CONSOLE_ID,  cfg->console_id);
    nvs_set_str(h, NVS_KEY_BRIDGE_HOST, cfg->bridge_host);
    nvs_set_u8 (h, NVS_KEY_TERM_IDX,   cfg->terminal_index);
    nvs_set_u16(h, NVS_KEY_BRIDGE_PORT, cfg->bridge_port);
    nvs_set_u8 (h, NVS_KEY_HID_EN,     (uint8_t)cfg->hid_enabled);
    esp_err_t ret = nvs_commit(h);
    nvs_close(h);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Config saved");
    }
    return ret;
}

esp_err_t sc_config_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset — erasing sc_config NVS namespace");
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    config_set_defaults(&s_cfg);
    return ESP_OK;
}

esp_err_t sc_config_ship_json_load(char **out_json, size_t *out_len)
{
    if (!out_json || !out_len) return ESP_ERR_INVALID_ARG;

    char path[80];
    snprintf(path, sizeof(path), "/spiffs/ships/%s.json", s_cfg.ship_id);

    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Ship JSON not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }

    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);

    *out_json = buf;
    *out_len  = (size_t)len;
    return ESP_OK;
}
