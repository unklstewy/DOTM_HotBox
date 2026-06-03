/*
 * sc_network.c — Wi-Fi + WebSocket client
 *
 * Wi-Fi connects via ESP32-C6 coprocessor (esp_hosted transparent API).
 * Credentials are read from NVS — never hard-coded.
 * WebSocket reconnects automatically every SC_NETWORK_WS_RECONNECT_MS.
 */

#include "sc_network.h"
#include "sc_config.h"
#include "sdkconfig.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "mdns.h"

static const char *TAG = "sc_network";

/* ── NVS keys (credentials) ─────────────────────────────────────────────── */
#define NVS_NS_CONFIG    "sc_config"
#define NVS_KEY_SSID     "ssid"
#define NVS_KEY_PSK      "wifi_psk"

/* ── Event group bits ────────────────────────────────────────────────────── */
#define NET_EVT_WIFI_GOT_IP   BIT0
#define NET_EVT_WS_CONNECTED  BIT1

/* ── State ───────────────────────────────────────────────────────────────── */
static sc_network_state_t        s_state = SC_NET_STATE_DISCONNECTED;
static EventGroupHandle_t        s_net_evg;
static esp_websocket_client_handle_t s_ws = NULL;

static sc_network_ws_rx_cb_t     s_ws_rx_cb  = NULL;
static void                     *s_ws_rx_ctx = NULL;
static sc_network_state_cb_t     s_state_cb  = NULL;
static void                     *s_state_ctx = NULL;

/* ── Task ────────────────────────────────────────────────────────────────── */
static StaticTask_t  s_task_buf;
static StackType_t   s_task_stack[SC_NETWORK_TASK_STACK_SIZE];
static TaskHandle_t  s_task_handle = NULL;

/* ── Forward declarations ────────────────────────────────────────────────── */
static void sc_network_task(void *arg);
static void sc_network_state_set(sc_network_state_t new_state);
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data);
static void sc_network_ws_event_handler(void *arg,
                                        esp_event_base_t base,
                                        int32_t event_id, void *data);
static esp_err_t wifi_connect(void);
static esp_err_t ws_connect(const char *host, uint16_t port);

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

esp_err_t sc_network_init(void)
{
    /* Suppress verbose Wi-Fi messages until we resume work on the network stack */
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set(TAG, ESP_LOG_ERROR);

    s_net_evg = xEventGroupCreate();
    if (!s_net_evg) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_err_t ret = wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connect failed: %s — will retry in task",
                 esp_err_to_name(ret));
    }

    s_task_handle = xTaskCreateStatic(
        sc_network_task,
        "sc_network",
        SC_NETWORK_TASK_STACK_SIZE,
        NULL,
        SC_NETWORK_TASK_PRIORITY,
        s_task_stack,
        &s_task_buf
    );
    if (!s_task_handle) return ESP_FAIL;

    ESP_LOGI(TAG, "Network subsystem started");
    return ESP_OK;
}

void sc_network_deinit(void)
{
    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }
    if (s_ws) {
        esp_websocket_client_stop(s_ws);
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
    }
    esp_wifi_stop();
    esp_wifi_deinit();
}

/* ── Registration ────────────────────────────────────────────────────────── */

esp_err_t sc_network_ws_rx_register(sc_network_ws_rx_cb_t cb, void *user_ctx)
{
    s_ws_rx_cb  = cb;
    s_ws_rx_ctx = user_ctx;
    return ESP_OK;
}

esp_err_t sc_network_state_register(sc_network_state_cb_t cb, void *user_ctx)
{
    s_state_cb  = cb;
    s_state_ctx = user_ctx;
    return ESP_OK;
}

/* ── Send ────────────────────────────────────────────────────────────────── */

esp_err_t sc_network_ws_send(const char *data, size_t len)
{
    if (s_state != SC_NET_STATE_WS_CONNECTED || !s_ws) {
        return ESP_ERR_INVALID_STATE;
    }
    int sent = esp_websocket_client_send_text(s_ws, data, (int)len,
                                              pdMS_TO_TICKS(1000));
    return (sent >= 0) ? ESP_OK : ESP_FAIL;
}

/* ── Query ───────────────────────────────────────────────────────────────── */

sc_network_state_t sc_network_state_get(void)
{
    return s_state;
}

/* ── State helper ────────────────────────────────────────────────────────── */

static void sc_network_state_set(sc_network_state_t new_state)
{
    if (s_state == new_state) return;
    s_state = new_state;
    if (s_state_cb) s_state_cb(new_state, s_state_ctx);
}

/* ── Wi-Fi ───────────────────────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        sc_network_state_set(SC_NET_STATE_DISCONNECTED);
        xEventGroupClearBits(s_net_evg, NET_EVT_WIFI_GOT_IP);
        ESP_LOGW(TAG, "Wi-Fi disconnected — reconnecting…");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        sc_network_state_set(SC_NET_STATE_WIFI_UP);
        xEventGroupSetBits(s_net_evg, NET_EVT_WIFI_GOT_IP);
        ESP_LOGI(TAG, "Wi-Fi connected, IP assigned");
    }
}

static esp_err_t wifi_connect(void)
{
    /* Read credentials from NVS */
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS_CONFIG, NVS_READONLY, &h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "NVS namespace '%s' not found", NVS_NS_CONFIG);
#if defined(CONFIG_SC_NETWORK_DEV_FALLBACK) && CONFIG_SC_NETWORK_DEV_FALLBACK
    ESP_LOGW(TAG, "Using dev fallback Wi-Fi credentials");
    goto wifi_fallback;
#else
    ESP_LOGW(TAG, "Wi-Fi not provisioned yet");
    return ESP_ERR_NOT_FOUND;
#endif
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", NVS_NS_CONFIG, esp_err_to_name(ret));
        return ret;
    }

    char ssid[SC_CONFIG_SSID_LEN] = {0};
    char psk[64] = {0};
    size_t len;

    len = sizeof(ssid);
    ret = nvs_get_str(h, NVS_KEY_SSID, ssid, &len);
    if (ret != ESP_OK || len <= 1) {
        nvs_close(h);
#if defined(CONFIG_SC_NETWORK_DEV_FALLBACK) && CONFIG_SC_NETWORK_DEV_FALLBACK
wifi_fallback:
        strlcpy(ssid, CONFIG_SC_NETWORK_DEV_SSID, sizeof(ssid));
        strlcpy(psk,  CONFIG_SC_NETWORK_DEV_PSK,  sizeof(psk));
        if (ssid[0] == '\0') {
            ESP_LOGW(TAG, "No Wi-Fi SSID in NVS and dev fallback SSID is empty — skipping Wi-Fi");
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGW(TAG, "No Wi-Fi SSID in NVS — using dev fallback SSID: %s", ssid);
        goto wifi_start;
#else
        ESP_LOGW(TAG, "No Wi-Fi SSID in NVS — provision via settings screen");
        return ESP_ERR_NOT_FOUND;
#endif
    }
    len = sizeof(psk);
    ret = nvs_get_str(h, NVS_KEY_PSK, psk, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "No Wi-Fi PSK in NVS; attempting connection without password");
        psk[0] = '\0';
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed reading Wi-Fi PSK from NVS: %s", esp_err_to_name(ret));
        psk[0] = '\0';
    }
    nvs_close(h);

#if defined(CONFIG_SC_NETWORK_DEV_FALLBACK) && CONFIG_SC_NETWORK_DEV_FALLBACK
wifi_start:
#endif
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WIFI_EVENT handler register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IP_EVENT handler register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid,     ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, psk,  sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sc_network_state_set(SC_NET_STATE_CONNECTING);
    return ESP_OK;
}

/* ── WebSocket ───────────────────────────────────────────────────────────── */

static void sc_network_ws_event_handler(void *arg, esp_event_base_t base,
                                         int32_t event_id, void *data)
{
    esp_websocket_event_data_t *ws_data = (esp_websocket_event_data_t *)data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            sc_network_state_set(SC_NET_STATE_WS_CONNECTED);
            xEventGroupSetBits(s_net_evg, NET_EVT_WS_CONNECTED);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected");
            sc_network_state_set(SC_NET_STATE_WIFI_UP);
            xEventGroupClearBits(s_net_evg, NET_EVT_WS_CONNECTED);
            break;
        case WEBSOCKET_EVENT_DATA:
            if (ws_data->op_code == 0x01 /* text */ && s_ws_rx_cb) {
                s_ws_rx_cb(ws_data->data_ptr, ws_data->data_len, s_ws_rx_ctx);
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            break;
        default:
            break;
    }
}

static esp_err_t ws_connect(const char *host, uint16_t port)
{
    char uri[128];
    snprintf(uri, sizeof(uri), "ws://%s:%d%s", host, port,
             SC_NETWORK_WS_PATH);

    const esp_websocket_client_config_t ws_cfg = {
        .uri            = uri,
        .buffer_size    = 4096,
        .task_stack     = 4096,
        .task_prio      = SC_NETWORK_TASK_PRIORITY - 1,
        .reconnect_timeout_ms = SC_NETWORK_WS_RECONNECT_MS,
    };

    if (s_ws) {
        esp_websocket_client_stop(s_ws);
        esp_websocket_client_destroy(s_ws);
    }

    s_ws = esp_websocket_client_init(&ws_cfg);
    if (!s_ws) return ESP_FAIL;

    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY,
                                  sc_network_ws_event_handler, NULL);
    return esp_websocket_client_start(s_ws);
}

/* ── Network task ────────────────────────────────────────────────────────── */

static void sc_network_task(void *arg)
{
    /* Wait for IP, then attempt mDNS discovery + WebSocket connect */
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_net_evg,
                                               NET_EVT_WIFI_GOT_IP,
                                               pdFALSE, pdTRUE,
                                               pdMS_TO_TICKS(30000));
        if (!(bits & NET_EVT_WIFI_GOT_IP)) {
            ESP_LOGW(TAG, "Still waiting for Wi-Fi…");
            continue;
        }

        /* Read bridge host/port from config */
        nvs_handle_t h;
        char bridge_host[SC_CONFIG_HOSTNAME_LEN] = "sc-bridge.local";
        uint16_t bridge_port = 8765;
        if (nvs_open(NVS_NS_CONFIG, NVS_READONLY, &h) == ESP_OK) {
            size_t len = sizeof(bridge_host);
            nvs_get_str(h, "bridge_host", bridge_host, &len);
            nvs_get_u16(h, "bridge_port", &bridge_port);
            nvs_close(h);
        }

        ESP_LOGI(TAG, "Connecting WebSocket → %s:%d", bridge_host, bridge_port);
        esp_err_t ret = ws_connect(bridge_host, bridge_port);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WebSocket connect failed — retrying in %d ms",
                     SC_NETWORK_WS_RECONNECT_MS);
        }

        /* Wait for disconnect then retry */
        xEventGroupWaitBits(s_net_evg, NET_EVT_WS_CONNECTED,
                            pdFALSE, pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(SC_NETWORK_WS_RECONNECT_MS));
    }
}
