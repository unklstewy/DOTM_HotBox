#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32P4
/**
 * @file wifi_hal_stubs.c
 * @brief Stub implementations for WiFi HAL functions not available on esp32_host targets.
 *
 * The prebuilt libnet80211.a (esp32_host variant) references HAL symbols that are
 * only implemented on chips with native WiFi silicon (ESP32, ESP32-S2, etc.).
 * On ESP32-P4, WiFi is hosted on the ESP32-C6 coprocessor via esp_hosted, so
 * these HAL entry points are never actually called at runtime — the linker just
 * requires a definition to be present.
 *
 * All stubs come from ieee80211_nan_common.o (NAN — Neighbor Awareness Networking).
 * NAN is a proximity-discovery feature of 802.11; on hosted targets all MAC/PHY
 * operations run on the ESP32-C6 coprocessor and these HAL paths are dead code.
 */

#include <stdint.h>
#include "esp_log.h"

static const char *TAG = "wifi_hal_stubs";

/* ── NAN TSF (Timing Synchronization Function) stubs ────────────────────── */

void hal_enable_nan_tsf(void)
{
    ESP_LOGD(TAG, "hal_enable_nan_tsf stub (no-op on esp32_host)");
}

void hal_disable_nan_tsf(void)
{
    ESP_LOGD(TAG, "hal_disable_nan_tsf stub (no-op on esp32_host)");
}

void hal_mac_tsf_reset(void)
{
    ESP_LOGD(TAG, "hal_mac_tsf_reset stub (no-op on esp32_host)");
}

void hal_mac_tsf_set_time(uint64_t time_us)
{
    (void)time_us;
    ESP_LOGD(TAG, "hal_mac_tsf_set_time stub (no-op on esp32_host)");
}

/* ── NAN MAC/scheduler stubs ─────────────────────────────────────────────── */

void *ic_get_G6M_sched(void)
{
    return NULL;
}

void ic_register_nan_callbacks(void *callbacks)
{
    (void)callbacks;
    ESP_LOGD(TAG, "ic_register_nan_callbacks stub (no-op on esp32_host)");
}
#endif
