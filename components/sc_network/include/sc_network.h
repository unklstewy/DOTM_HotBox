/*
 * sc_network.h — Wi-Fi + WebSocket client
 *
 * Manages:
 *   - Wi-Fi connection via ESP32-C6 coprocessor (esp_hosted transparent API)
 *   - mDNS service discovery for PC bridge ("_sc_bridge._tcp")
 *   - WebSocket client → PC bridge (receives game events, sends terminal state)
 *
 * Wi-Fi credentials are read from NVS (namespace "sc_config", keys "ssid"/"wifi_psk").
 * They are NEVER embedded in firmware source.
 *
 * Task priority: SC_NETWORK_TASK_PRIORITY (7)
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ──────────────────────────────────────────────────────────── */
#define SC_NETWORK_TASK_STACK_SIZE   (6144)
#define SC_NETWORK_TASK_PRIORITY     (7)
#define SC_NETWORK_MDNS_SERVICE      "_sc_bridge"
#define SC_NETWORK_MDNS_PROTO        "_tcp"
#define SC_NETWORK_WS_PATH           "/terminal"
#define SC_NETWORK_WS_RECONNECT_MS   (5000)

/* ── State ──────────────────────────────────────────────────────────────── */
typedef enum {
    SC_NET_STATE_DISCONNECTED = 0,
    SC_NET_STATE_CONNECTING,
    SC_NET_STATE_WIFI_UP,
    SC_NET_STATE_AP_UP,
    SC_NET_STATE_WS_CONNECTED,
} sc_network_state_t;

/* ── Callbacks ──────────────────────────────────────────────────────────── */

/** Called when a WebSocket message arrives from the PC bridge. */
typedef void (*sc_network_ws_rx_cb_t)(const char *data, size_t len,
                                      void *user_ctx);

/** Called when the network state changes. */
typedef void (*sc_network_state_cb_t)(sc_network_state_t state,
                                      void *user_ctx);

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/** @brief Initialise Wi-Fi, mDNS, WebSocket client and start network task. */
esp_err_t sc_network_init(void);

/** @brief Shut down all network subsystems. */
void sc_network_deinit(void);

/* ── Registration ───────────────────────────────────────────────────────── */

/** @brief Register WebSocket receive callback (called from network task). */
esp_err_t sc_network_ws_rx_register(sc_network_ws_rx_cb_t cb, void *user_ctx);

/** @brief Register network state change callback. */
esp_err_t sc_network_state_register(sc_network_state_cb_t cb, void *user_ctx);

/* ── Send ───────────────────────────────────────────────────────────────── */

/**
 * @brief Send a UTF-8 text frame to the PC bridge.
 * @return ESP_ERR_INVALID_STATE if WebSocket not connected.
 */
esp_err_t sc_network_ws_send(const char *data, size_t len);

/* ── Query ──────────────────────────────────────────────────────────────── */

/** @brief Return current network state (thread-safe). */
sc_network_state_t sc_network_state_get(void);

/** @brief Return if network is currently in AP mode. */
bool sc_network_is_ap(void);

/** @brief Update Wi-Fi credentials in NVS. */
esp_err_t sc_network_set_wifi_credentials(const char *ssid, const char *psk);

/** @brief Run Wi-Fi scan and write results to JSON buffer. */
esp_err_t sc_network_scan_wifi(char *buf, size_t max_len);

#ifdef __cplusplus
}
#endif
