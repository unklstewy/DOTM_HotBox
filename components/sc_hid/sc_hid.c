/*
 * sc_hid.c — TinyUSB composite HID device (Keyboard + Consumer Control)
 *
 * Manages the action table loaded from ship JSON and dispatches HID reports
 * over USB. The HID task runs at priority 10 (highest) for sub-2ms latency.
 */

#include "sc_hid.h"
#include "sc_hid_desc.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "tinyusb.h"             /* esp_tinyusb wrapper: tinyusb_config_t, tinyusb_driver_install */
#include "class/hid/hid_device.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "sc_hid";

/* ── Action table ────────────────────────────────────────────────────────── */
static sc_hid_action_t  s_actions[SC_HID_MAX_ACTIONS];
static size_t           s_action_count = 0;
static SemaphoreHandle_t s_action_mutex;

/* ── USB task ────────────────────────────────────────────────────────────── */
static StaticTask_t  s_usb_task_buf;
static StackType_t   s_usb_task_stack[SC_HID_TASK_STACK_SIZE];
static TaskHandle_t  s_usb_task_handle = NULL;

/* ── Internal queue for action dispatch ─────────────────────────────────── */
typedef struct {
    sc_hid_action_t action;
    uint32_t        hold_ms;
} hid_dispatch_t;

static QueueHandle_t s_dispatch_q;

/* ── Forward declarations ────────────────────────────────────────────────── */
static void        sc_hid_usb_task(void *arg);
static esp_err_t   sc_hid_action_find(const char *id, sc_hid_action_t *out);

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

esp_err_t sc_hid_init(void)
{
    s_action_mutex = xSemaphoreCreateMutex();
    if (!s_action_mutex) return ESP_ERR_NO_MEM;

    s_dispatch_q = xQueueCreate(8, sizeof(hid_dispatch_t));
    if (!s_dispatch_q) return ESP_ERR_NO_MEM;

    /* esp_tinyusb 1.7+ requires explicit descriptor pointers in config. */
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor        = sc_hid_get_device_descriptor(),
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = sc_hid_get_configuration_descriptor(),
        .hs_configuration_descriptor = sc_hid_get_hs_configuration_descriptor(),
        .qualifier_descriptor        = sc_hid_get_qualifier_descriptor(),
#else
        .configuration_descriptor = sc_hid_get_configuration_descriptor(),
#endif
        .string_descriptor        = sc_hid_get_string_descriptors(),
        .string_descriptor_count  = sc_hid_get_string_descriptor_count(),
        .external_phy             = false,
    };
    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_usb_task_handle = xTaskCreateStatic(
        sc_hid_usb_task,
        "sc_hid",
        SC_HID_TASK_STACK_SIZE,
        NULL,
        SC_HID_TASK_PRIORITY,
        s_usb_task_stack,
        &s_usb_task_buf
    );
    if (!s_usb_task_handle) return ESP_FAIL;

    ESP_LOGI(TAG, "HID subsystem started");
    return ESP_OK;
}

void sc_hid_deinit(void)
{
    if (s_usb_task_handle) {
        vTaskDelete(s_usb_task_handle);
        s_usb_task_handle = NULL;
    }
    tinyusb_driver_uninstall();
}

/* ── Action table ────────────────────────────────────────────────────────── */

esp_err_t sc_hid_action_table_load(const sc_hid_action_t *actions, size_t count)
{
    if (!actions || count > SC_HID_MAX_ACTIONS) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_action_mutex, portMAX_DELAY);
    memcpy(s_actions, actions, count * sizeof(sc_hid_action_t));
    s_action_count = count;
    xSemaphoreGive(s_action_mutex);

    ESP_LOGI(TAG, "Action table loaded: %d actions", (int)count);
    return ESP_OK;
}

/* ── Action dispatch ─────────────────────────────────────────────────────── */

esp_err_t sc_hid_action_send(const char *action_id)
{
    sc_hid_action_t action;
    esp_err_t ret = sc_hid_action_find(action_id, &action);
    if (ret != ESP_OK) return ret;

    hid_dispatch_t msg = { .action = action, .hold_ms = 0 };
    if (xQueueSend(s_dispatch_q, &msg, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "HID dispatch queue full — dropping action: %s", action_id);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t sc_hid_action_hold(const char *action_id, uint32_t override_ms)
{
    sc_hid_action_t action;
    esp_err_t ret = sc_hid_action_find(action_id, &action);
    if (ret != ESP_OK) return ret;

    hid_dispatch_t msg = {
        .action  = action,
        .hold_ms = override_ms ? override_ms : action.hold_ms
    };
    if (xQueueSend(s_dispatch_q, &msg, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

/* ── Low-level reports ───────────────────────────────────────────────────── */

esp_err_t sc_hid_report_keyboard_send(uint8_t modifier, const uint8_t keycodes[6])
{
    if (!tud_hid_ready()) return ESP_ERR_INVALID_STATE;
    tud_hid_keyboard_report(1, modifier, (uint8_t *)keycodes);
    return ESP_OK;
}

esp_err_t sc_hid_report_keyboard_release(void)
{
    if (!tud_hid_ready()) return ESP_ERR_INVALID_STATE;
    static const uint8_t zeros[6] = {0};
    tud_hid_keyboard_report(1, 0, (uint8_t *)zeros);
    return ESP_OK;
}

esp_err_t sc_hid_report_consumer_send(uint16_t usage)
{
    if (!tud_hid_ready()) return ESP_ERR_INVALID_STATE;
    tud_hid_report(2, &usage, sizeof(usage));
    return ESP_OK;
}

/* ── USB task ────────────────────────────────────────────────────────────── */

static void sc_hid_usb_task(void *arg)
{
    hid_dispatch_t msg;
    for (;;) {
        /* Drive TinyUSB */
        tud_task();

        /* Drain dispatch queue */
        while (xQueueReceive(s_dispatch_q, &msg, 0) == pdTRUE) {
            if (!tud_hid_ready()) continue;

            uint32_t hold = msg.hold_ms ? msg.hold_ms : 20; /* default 20 ms tap */

            if (msg.action.consumer_usage) {
                sc_hid_report_consumer_send(msg.action.consumer_usage);
                vTaskDelay(pdMS_TO_TICKS(hold));
                uint16_t zero = 0;
                tud_hid_report(2, &zero, sizeof(zero));
            } else {
                sc_hid_report_keyboard_send(msg.action.modifier,
                                            msg.action.keycodes);
                vTaskDelay(pdMS_TO_TICKS(hold));
                sc_hid_report_keyboard_release();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static esp_err_t sc_hid_action_find(const char *id, sc_hid_action_t *out)
{
    xSemaphoreTake(s_action_mutex, portMAX_DELAY);
    for (size_t i = 0; i < s_action_count; i++) {
        if (strcmp(s_actions[i].action_id, id) == 0) {
            *out = s_actions[i];
            xSemaphoreGive(s_action_mutex);
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_action_mutex);
    ESP_LOGW(TAG, "Action not found: %s", id);
    return ESP_ERR_NOT_FOUND;
}
