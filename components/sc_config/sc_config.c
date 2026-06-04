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
#define NVS_KEY_DISP_ROT     "disp_rot"

/* Touch calibration keys */
#define NVS_KEY_TC_IS_CAL    "tc_is_cal"
#define NVS_KEY_TC_X_MIN     "tc_x_min"
#define NVS_KEY_TC_X_MAX     "tc_x_max"
#define NVS_KEY_TC_Y_MIN     "tc_y_min"
#define NVS_KEY_TC_Y_MAX     "tc_y_max"
#define NVS_KEY_TC_SWAP      "tc_swap"
#define NVS_KEY_TC_INV_X     "tc_inv_x"
#define NVS_KEY_TC_INV_Y     "tc_inv_y"

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
    cfg->touch_cal.is_calibrated = false;
    cfg->touch_cal.x_min = 0;
    cfg->touch_cal.x_max = 800;
    cfg->touch_cal.y_min = 0;
    cfg->touch_cal.y_max = 1280;
    cfg->touch_cal.swap_xy = false;
    cfg->touch_cal.invert_x = false;
    cfg->touch_cal.invert_y = false;
    cfg->display_rotation = 0;
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

    uint8_t tc_bool;
    int32_t i32;
    if (nvs_get_u8(h, NVS_KEY_TC_IS_CAL, &tc_bool) == ESP_OK) cfg->touch_cal.is_calibrated = (bool)tc_bool;
    if (nvs_get_i32(h, NVS_KEY_TC_X_MIN, &i32) == ESP_OK) cfg->touch_cal.x_min = i32;
    if (nvs_get_i32(h, NVS_KEY_TC_X_MAX, &i32) == ESP_OK) cfg->touch_cal.x_max = i32;
    if (nvs_get_i32(h, NVS_KEY_TC_Y_MIN, &i32) == ESP_OK) cfg->touch_cal.y_min = i32;
    if (nvs_get_i32(h, NVS_KEY_TC_Y_MAX, &i32) == ESP_OK) cfg->touch_cal.y_max = i32;
    if (nvs_get_u8(h, NVS_KEY_TC_SWAP, &tc_bool) == ESP_OK) cfg->touch_cal.swap_xy = (bool)tc_bool;
    if (nvs_get_u8(h, NVS_KEY_TC_INV_X, &tc_bool) == ESP_OK) cfg->touch_cal.invert_x = (bool)tc_bool;
    if (nvs_get_u8(h, NVS_KEY_TC_INV_Y, &tc_bool) == ESP_OK) cfg->touch_cal.invert_y = (bool)tc_bool;

    uint8_t rot;
    if (nvs_get_u8(h, NVS_KEY_DISP_ROT, &rot) == ESP_OK) {
        cfg->display_rotation = rot;
    } else {
        cfg->display_rotation = 0;
    }

    nvs_close(h);
    return ESP_OK;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t sc_config_init(void)
{
    if (s_initialised) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;



    /* Load terminal identity from NVS */
    config_set_defaults(&s_cfg);
    esp_err_t ret = nvs_load(&s_cfg);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Config loaded: ship=%s console=%s terminal=%d",
             s_cfg.ship_id, s_cfg.console_id, s_cfg.terminal_index);

    s_initialised = true;
    return ESP_OK;
}

void sc_config_deinit(void)
{
    if (!s_initialised) return;
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

    nvs_set_u8(h, NVS_KEY_TC_IS_CAL, (uint8_t)cfg->touch_cal.is_calibrated);
    nvs_set_i32(h, NVS_KEY_TC_X_MIN, cfg->touch_cal.x_min);
    nvs_set_i32(h, NVS_KEY_TC_X_MAX, cfg->touch_cal.x_max);
    nvs_set_i32(h, NVS_KEY_TC_Y_MIN, cfg->touch_cal.y_min);
    nvs_set_i32(h, NVS_KEY_TC_Y_MAX, cfg->touch_cal.y_max);
    nvs_set_u8(h, NVS_KEY_TC_SWAP, (uint8_t)cfg->touch_cal.swap_xy);
    nvs_set_u8(h, NVS_KEY_TC_INV_X, (uint8_t)cfg->touch_cal.invert_x);
    nvs_set_u8(h, NVS_KEY_TC_INV_Y, (uint8_t)cfg->touch_cal.invert_y);
    nvs_set_u8(h, NVS_KEY_DISP_ROT, cfg->display_rotation);

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
    snprintf(path, sizeof(path), "/sdcard/ships/%s.json", s_cfg.ship_id);

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
