/*
 * sc_terminal — main.c
 * Entry point for the Star Citizen Terminal firmware.
 *
 * Boot sequence
 * ─────────────
 *  1. NVS init  (persist ship/terminal config)
 *  2. sc_config — load active ship + console layout
 *  3. sc_hid    — USB HID composite device (keyboard + consumer)
 *  4. sc_ui     — LVGL display engine + touch driver
 *  5. sc_network — Wi-Fi via ESP32-C6 coprocessor, then WebSocket to PC bridge
 *  6. sc_gamelink — Game.log event subscriber (via network)
 *
 * All modules expose FreeRTOS task handles so they can be suspended
 * individually if a component is not enabled in the active config.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "sc_config.h"
#include "sc_storage.h"
#include "sc_hid.h"
#include "sc_ui.h"
#include "sc_network.h"
#include "sc_gamelink.h"
#include "sc_web.h"

static const char *TAG = "sc_main";

/* ── Forward declarations ────────────────────────────────────────────────── */
static esp_err_t nvs_init(void);

/* ── app_main ────────────────────────────────────────────────────────────── */
void app_main(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "SC Terminal boot — v%d.%d.%d",
             SC_VER_MAJOR, SC_VER_MINOR, SC_VER_PATCH);

    /* ── 1. NVS ─────────────────────────────────────────────────────────── */
    ESP_ERROR_CHECK(nvs_init());

    /* ── 1.5. Storage (SD Card) ─────────────────────────────────────────── */
    err = sc_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Storage init failed: %s", esp_err_to_name(err));
        // Continue anyway, maybe no SD card inserted
    }

    /* ── 2. Config ──────────────────────────────────────────────────────── */
    ESP_ERROR_CHECK(sc_config_init());
    const sc_terminal_config_t *cfg = sc_config_get();
    ESP_LOGI(TAG, "Ship: %s  Console: %s  Terminal: %d",
             cfg->ship_id, cfg->console_id, cfg->terminal_index);

    /* ── 3. HID ─────────────────────────────────────────────────────────── */
    err = sc_hid_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HID init failed, continuing without USB HID: %s", esp_err_to_name(err));
    }

    /* ── 4. UI ──────────────────────────────────────────────────────────── */
    ESP_ERROR_CHECK(sc_ui_init(cfg));

    /* ── 5. Network (Wi-Fi + WebSocket) ─────────────────────────────────── */
    ESP_ERROR_CHECK(sc_network_init());
    ESP_ERROR_CHECK(sc_web_start());

    /* ── 6. Gamelink ────────────────────────────────────────────────────── */
    ESP_ERROR_CHECK(sc_gamelink_init());

    ESP_LOGI(TAG, "All subsystems started.");

    /* Main task idles — subsystem tasks own their loops. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static esp_err_t nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated — erasing and reinitialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}
