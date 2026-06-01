/*
 * sc_config.h — Ship & terminal configuration persistence
 *
 * Loads the active ship JSON from SPIFFS and the terminal identity
 * (ship_id, console_id, terminal_index, Wi-Fi credentials) from NVS.
 *
 * NVS namespace: "sc_config"
 * SPIFFS mount : /spiffs
 * Ship JSON    : /spiffs/ships/<ship_id>.json
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version stamp (matches CMakeLists VERSION) ─────────────────────────── */
#define SC_VER_MAJOR  (1)
#define SC_VER_MINOR  (0)
#define SC_VER_PATCH  (0)

/* ── Constants ──────────────────────────────────────────────────────────── */
#define SC_CONFIG_SHIP_ID_LEN      (32)
#define SC_CONFIG_CONSOLE_ID_LEN   (48)
#define SC_CONFIG_HOSTNAME_LEN     (64)
#define SC_CONFIG_SSID_LEN         (32)

/* ── Terminal identity (stored in NVS) ──────────────────────────────────── */
typedef struct {
    char    ship_id[SC_CONFIG_SHIP_ID_LEN];       /**< e.g. "cutlass_black"   */
    char    console_id[SC_CONFIG_CONSOLE_ID_LEN]; /**< e.g. "pilot_mfd_left"  */
    uint8_t terminal_index;                       /**< 0-based index (0–3)    */
    char    bridge_host[SC_CONFIG_HOSTNAME_LEN];  /**< PC bridge hostname/IP  */
    uint16_t bridge_port;                         /**< WebSocket port (default 8765) */
    bool    hid_enabled;                          /**< USB HID active         */
} sc_terminal_config_t;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/**
 * @brief Mount SPIFFS, open NVS, load terminal config.
 *        Must be called after nvs_flash_init().
 */
esp_err_t sc_config_init(void);

/** @brief Flush any pending NVS writes and unmount SPIFFS. */
void sc_config_deinit(void);

/* ── Accessors ──────────────────────────────────────────────────────────── */

/** @brief Return pointer to the loaded terminal config (read-only). */
const sc_terminal_config_t *sc_config_get(void);

/**
 * @brief Persist updated terminal config to NVS.
 * @param cfg  New config to save.
 */
esp_err_t sc_config_save(const sc_terminal_config_t *cfg);

/**
 * @brief Reset NVS to factory defaults and reload.
 *        Useful after a failed pairing or corrupt state.
 */
esp_err_t sc_config_factory_reset(void);

/* ── Ship JSON helpers ──────────────────────────────────────────────────── */

/**
 * @brief Return the raw JSON string for the active ship.
 *        Caller must free() the returned buffer.
 * @param[out] out_json  Pointer to allocated JSON string.
 * @param[out] out_len   Length of the JSON string.
 */
esp_err_t sc_config_ship_json_load(char **out_json, size_t *out_len);

#ifdef __cplusplus
}
#endif
