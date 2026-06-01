/*
 * sc_ui.h — LVGL display engine public API
 *
 * Owns:
 *   - MIPI-DSI panel init (ESP32-P4 LCD peripheral)
 *   - Goodix GT911 touch driver (I²C)
 *   - LVGL 9 task + tick source
 *   - Screen router
 *
 * ALL LVGL calls outside this component must use lv_lock()/lv_unlock().
 * Frame buffers are allocated in PSRAM (MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA).
 *
 * Display: 800 × 1280 px, 16-bit RGB565, portrait
 * Task priority: SC_UI_TASK_PRIORITY (5)
 */
#pragma once

#include "esp_err.h"
#include "sc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ──────────────────────────────────────────────────────────── */
#define SC_UI_TASK_STACK_SIZE     (8192)
#define SC_UI_TASK_PRIORITY       (5)
#define SC_UI_DISPLAY_WIDTH       (800)
#define SC_UI_DISPLAY_HEIGHT      (1280)
#define SC_UI_FB_SIZE             (SC_UI_DISPLAY_WIDTH * SC_UI_DISPLAY_HEIGHT * 2)

/* ── Screen IDs ─────────────────────────────────────────────────────────── */
typedef enum {
    SC_UI_SCREEN_PAIRING = 0,    /**< Wi-Fi / bridge pairing wizard    */
    SC_UI_SCREEN_CONSOLE,        /**< Main ship console MFD layout     */
    SC_UI_SCREEN_SETTINGS,       /**< Terminal settings                 */
    SC_UI_SCREEN_OTA,            /**< OTA update progress               */
    SC_UI_SCREEN_COUNT
} sc_ui_screen_id_t;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/**
 * @brief Initialise MIPI-DSI panel, GT911 touch, LVGL, and screen router.
 *        Starts the LVGL task. Must be called after sc_config_init().
 * @param cfg  Active terminal config (selects initial screen/layout).
 */
esp_err_t sc_ui_init(const sc_terminal_config_t *cfg);

/** @brief Stop LVGL task and release display resources. */
void sc_ui_deinit(void);

/* ── Screen Router ──────────────────────────────────────────────────────── */

/** @brief Navigate to a screen, pushing it onto the history stack. */
esp_err_t sc_ui_router_push(sc_ui_screen_id_t screen_id);

/** @brief Navigate back to the previous screen. */
esp_err_t sc_ui_router_pop(void);

/** @brief Jump to the ship console root screen. */
esp_err_t sc_ui_router_home(void);

/* ── Brightness ─────────────────────────────────────────────────────────── */

/**
 * @brief Set display brightness.
 * @param percent  0–100
 */
esp_err_t sc_ui_brightness_set(uint8_t percent);

#ifdef __cplusplus
}
#endif
