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

/* Low-level register access for USB PHY software override */
#include "soc/soc.h"
#include "soc/lp_system_reg.h"
#include "sc_config.h"

static const char *TAG = "sc_hid";

/* ── Action table ────────────────────────────────────────────────────────── */
static sc_hid_action_t  s_actions[SC_HID_MAX_ACTIONS];
static size_t           s_action_count = 0;
static SemaphoreHandle_t s_action_mutex;

/* ── USB & Dispatch tasks ────────────────────────────────────────────────── */
static StaticTask_t  s_usb_task_buf;
static StackType_t   s_usb_task_stack[SC_HID_TASK_STACK_SIZE];
static TaskHandle_t  s_usb_task_handle = NULL;

static StaticTask_t  s_dispatch_task_buf;
static StackType_t   s_dispatch_task_stack[SC_HID_TASK_STACK_SIZE];
static TaskHandle_t  s_dispatch_task_handle = NULL;

/* ── Internal queue for action dispatch ─────────────────────────────────── */
typedef enum {
    HID_CMD_PRESS,
    HID_CMD_RELEASE,
    HID_CMD_PULSE
} hid_cmd_type_t;

typedef struct {
    hid_cmd_type_t  cmd;
    sc_hid_action_t action;
    uint32_t        hold_ms;
} hid_dispatch_t;

static QueueHandle_t s_dispatch_q;

/* ── Forward declarations ────────────────────────────────────────────────── */
static void        sc_hid_usb_task(void *arg);
static void        sc_hid_dispatch_task(void *arg);
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

    /* Software override to route FS_PHY1 (GPIO 24/25, USB-C connector) to USB-OTG FS.
     * By default, FS_PHY1 connects to USB Serial/JTAG, and FS_PHY2 (GPIO 26/27) to USB-OTG.
     * We swap them by writing to LP_SYSTEM_REG_USB_CTRL_REG:
     * - LP_SYSTEM_REG_SW_HW_USB_PHY_SEL = 1 (enable software override of the eFuse)
     * - LP_SYSTEM_REG_SW_USB_PHY_SEL = 1 (route FS_PHY1 to OTG, FS_PHY2 to Serial/JTAG)
     */
    if (sc_config_get()->hid_enabled) {
        uint32_t usb_ctrl = REG_READ(LP_SYSTEM_REG_USB_CTRL_REG);
        usb_ctrl |= LP_SYSTEM_REG_SW_HW_USB_PHY_SEL;
        usb_ctrl |= LP_SYSTEM_REG_SW_USB_PHY_SEL;
        REG_WRITE(LP_SYSTEM_REG_USB_CTRL_REG, usb_ctrl);
    }

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_usb_task_handle = xTaskCreateStatic(
        sc_hid_usb_task,
        "sc_hid_usb",
        SC_HID_TASK_STACK_SIZE,
        NULL,
        SC_HID_TASK_PRIORITY,
        s_usb_task_stack,
        &s_usb_task_buf
    );
    if (!s_usb_task_handle) return ESP_FAIL;

    s_dispatch_task_handle = xTaskCreateStatic(
        sc_hid_dispatch_task,
        "sc_hid_disp",
        SC_HID_TASK_STACK_SIZE,
        NULL,
        SC_HID_TASK_PRIORITY - 1,
        s_dispatch_task_stack,
        &s_dispatch_task_buf
    );
    if (!s_dispatch_task_handle) {
        vTaskDelete(s_usb_task_handle);
        s_usb_task_handle = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "HID subsystem started");
    return ESP_OK;
}

void sc_hid_deinit(void)
{
    if (s_usb_task_handle) {
        vTaskDelete(s_usb_task_handle);
        s_usb_task_handle = NULL;
    }
    if (s_dispatch_task_handle) {
        vTaskDelete(s_dispatch_task_handle);
        s_dispatch_task_handle = NULL;
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
    if (!tud_ready()) return ESP_ERR_INVALID_STATE;

    sc_hid_action_t action;
    esp_err_t ret = sc_hid_action_find(action_id, &action);
    if (ret != ESP_OK) return ret;

    uint32_t hold = action.hold_ms > 0 ? action.hold_ms : 50; /* default 50ms pulse */
    hid_dispatch_t msg = { .cmd = HID_CMD_PULSE, .action = action, .hold_ms = hold };
    if (xQueueSend(s_dispatch_q, &msg, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "HID dispatch queue full — dropping action: %s", action_id);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t sc_hid_action_hold(const char *action_id, uint32_t override_ms)
{
    if (!tud_ready()) return ESP_ERR_INVALID_STATE;

    sc_hid_action_t action;
    esp_err_t ret = sc_hid_action_find(action_id, &action);
    if (ret != ESP_OK) return ret;

    uint32_t hold = override_ms ? override_ms : (action.hold_ms > 0 ? action.hold_ms : 50);
    hid_dispatch_t msg = {
        .cmd = HID_CMD_PULSE,
        .action  = action,
        .hold_ms = hold
    };
    if (xQueueSend(s_dispatch_q, &msg, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t sc_hid_action_press(const char *action_id)
{
    if (!tud_ready()) return ESP_ERR_INVALID_STATE;

    sc_hid_action_t action;
    esp_err_t ret = sc_hid_action_find(action_id, &action);
    if (ret != ESP_OK) return ret;

    hid_dispatch_t msg = { .cmd = HID_CMD_PRESS, .action = action, .hold_ms = 0 };
    if (xQueueSend(s_dispatch_q, &msg, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t sc_hid_action_release(const char *action_id)
{
    if (!tud_ready()) return ESP_ERR_INVALID_STATE;

    sc_hid_action_t action;
    esp_err_t ret = sc_hid_action_find(action_id, &action);
    if (ret != ESP_OK) return ret;

    hid_dispatch_t msg = { .cmd = HID_CMD_RELEASE, .action = action, .hold_ms = 0 };
    if (xQueueSend(s_dispatch_q, &msg, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

/* ── Low-level reports ───────────────────────────────────────────────────── */

esp_err_t sc_hid_report_gamepad_send(uint32_t buttons)
{
    if (!tud_hid_ready()) return ESP_ERR_INVALID_STATE;
    struct TU_ATTR_PACKED {
        uint32_t buttons;
        int8_t   x;
        int8_t   y;
    } report = {
        .buttons = buttons,
        .x = 0,
        .y = 0
    };
    tud_hid_report(1, &report, sizeof(report));
    return ESP_OK;
}

esp_err_t sc_hid_report_gamepad_release(void)
{
    return sc_hid_report_gamepad_send(0);
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
    for (;;) {
        /* Drive TinyUSB */
        tud_task();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ── Queue Dispatch task ─────────────────────────────────────────────────── */

static void sc_hid_dispatch_task(void *arg)
{
    uint32_t current_buttons = 0;
    uint32_t button_release_time[32] = {0};
    uint16_t current_consumer = 0;
    uint32_t consumer_release_time = 0;

    uint32_t last_sent_buttons = 0xFFFFFFFF; /* force initial send */
    uint16_t last_sent_consumer = 0xFFFF;

    hid_dispatch_t msg;

    for (;;) {
        uint32_t now = esp_log_timestamp();

        /* Check auto-releases for gamepad buttons */
        for (int i = 0; i < 32; i++) {
            if (button_release_time[i] > 0 && now >= button_release_time[i]) {
                current_buttons &= ~(1UL << i);
                button_release_time[i] = 0;
            }
        }

        /* Check auto-release for consumer control */
        if (consumer_release_time > 0 && now >= consumer_release_time) {
            current_consumer = 0;
            consumer_release_time = 0;
        }

        /* Wait up to 10ms for a command */
        if (xQueueReceive(s_dispatch_q, &msg, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (msg.cmd == HID_CMD_PRESS) {
                if (msg.action.gamepad_button > 0 && msg.action.gamepad_button <= 32) {
                    int idx = msg.action.gamepad_button - 1;
                    current_buttons |= (1UL << idx);
                    button_release_time[idx] = 0; /* cancel any active auto-release */
                } else if (msg.action.consumer_usage > 0) {
                    current_consumer = msg.action.consumer_usage;
                    consumer_release_time = 0;
                }
            } else if (msg.cmd == HID_CMD_RELEASE) {
                if (msg.action.gamepad_button > 0 && msg.action.gamepad_button <= 32) {
                    int idx = msg.action.gamepad_button - 1;
                    current_buttons &= ~(1UL << idx);
                    button_release_time[idx] = 0;
                } else if (msg.action.consumer_usage > 0) {
                    if (current_consumer == msg.action.consumer_usage) {
                        current_consumer = 0;
                    }
                    consumer_release_time = 0;
                }
            } else if (msg.cmd == HID_CMD_PULSE) {
                uint32_t hold = msg.hold_ms > 0 ? msg.hold_ms : 50;
                if (msg.action.gamepad_button > 0 && msg.action.gamepad_button <= 32) {
                    int idx = msg.action.gamepad_button - 1;
                    current_buttons |= (1UL << idx);
                    button_release_time[idx] = now + hold;
                } else if (msg.action.consumer_usage > 0) {
                    current_consumer = msg.action.consumer_usage;
                    consumer_release_time = now + hold;
                }
            }
        }

        /* Send reports on changes */
        if (tud_hid_ready()) {
            if (current_buttons != last_sent_buttons) {
                if (sc_hid_report_gamepad_send(current_buttons) == ESP_OK) {
                    last_sent_buttons = current_buttons;
                }
            }
            if (current_consumer != last_sent_consumer) {
                if (sc_hid_report_consumer_send(current_consumer) == ESP_OK) {
                    last_sent_consumer = current_consumer;
                }
            }
        }
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

void sc_hid_set_phy_swap(bool enable)
{
    uint32_t usb_ctrl = REG_READ(LP_SYSTEM_REG_USB_CTRL_REG);
    if (enable) {
        ESP_LOGI(TAG, "Enabling USB PHY override: swapping FS_PHY1 to USB-OTG");
        usb_ctrl |= LP_SYSTEM_REG_SW_HW_USB_PHY_SEL;
        usb_ctrl |= LP_SYSTEM_REG_SW_USB_PHY_SEL;
    } else {
        ESP_LOGI(TAG, "Disabling USB PHY override: reverting FS_PHY1 to USB Serial/JTAG");
        usb_ctrl &= ~LP_SYSTEM_REG_SW_HW_USB_PHY_SEL;
        usb_ctrl &= ~LP_SYSTEM_REG_SW_USB_PHY_SEL;
    }
    REG_WRITE(LP_SYSTEM_REG_USB_CTRL_REG, usb_ctrl);
}
