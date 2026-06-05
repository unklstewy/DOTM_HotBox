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
#if CONFIG_IDF_TARGET_ESP32P4
#include "soc/lp_system_reg.h"
#endif
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
#if CONFIG_IDF_TARGET_ESP32P4
    if (sc_config_get()->hid_enabled) {
        uint32_t usb_ctrl = REG_READ(LP_SYSTEM_REG_USB_CTRL_REG);
        usb_ctrl |= LP_SYSTEM_REG_SW_HW_USB_PHY_SEL;
        usb_ctrl |= LP_SYSTEM_REG_SW_USB_PHY_SEL;
        REG_WRITE(LP_SYSTEM_REG_USB_CTRL_REG, usb_ctrl);
    }
#endif

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

esp_err_t sc_hid_raw_button_press(uint16_t button)
{
    if (!tud_ready()) return ESP_ERR_INVALID_STATE;
    if (button < 1 || button > 256) return ESP_ERR_INVALID_ARG;

    sc_hid_action_t action = {0};
    action.gamepad_button = button;

    hid_dispatch_t msg = { .cmd = HID_CMD_PRESS, .action = action, .hold_ms = 0 };
    if (xQueueSend(s_dispatch_q, &msg, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t sc_hid_raw_button_release(uint16_t button)
{
    if (!tud_ready()) return ESP_ERR_INVALID_STATE;
    if (button < 1 || button > 256) return ESP_ERR_INVALID_ARG;

    sc_hid_action_t action = {0};
    action.gamepad_button = button;

    hid_dispatch_t msg = { .cmd = HID_CMD_RELEASE, .action = action, .hold_ms = 0 };
    if (xQueueSend(s_dispatch_q, &msg, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t sc_hid_raw_button_pulse(uint16_t button, uint32_t hold_ms)
{
    if (!tud_ready()) return ESP_ERR_INVALID_STATE;
    if (button < 1 || button > 256) return ESP_ERR_INVALID_ARG;

    sc_hid_action_t action = {0};
    action.gamepad_button = button;

    uint32_t hold = hold_ms > 0 ? hold_ms : 50;
    hid_dispatch_t msg = { .cmd = HID_CMD_PULSE, .action = action, .hold_ms = hold };
    if (xQueueSend(s_dispatch_q, &msg, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

/* ── Low-level reports ───────────────────────────────────────────────────── */

struct TU_ATTR_PACKED gamepad_report_t {
    uint8_t buttons[16];   /* 128 buttons */
    uint8_t axes[8];       /* 8 axes */
    uint8_t hat;           /* 4 bits POV, 4 bits padding */
};

esp_err_t sc_hid_report_gamepad_a_send(const struct gamepad_report_t *rep)
{
    if (!tud_hid_ready()) return ESP_ERR_INVALID_STATE;
    tud_hid_report(1, rep, sizeof(struct gamepad_report_t));
    return ESP_OK;
}

esp_err_t sc_hid_report_gamepad_b_send(const struct gamepad_report_t *rep)
{
    if (!tud_hid_ready()) return ESP_ERR_INVALID_STATE;
    tud_hid_report(2, rep, sizeof(struct gamepad_report_t));
    return ESP_OK;
}

esp_err_t sc_hid_report_gamepad_send(uint32_t buttons)
{
    /* Deprecated but kept for compatibility. Sends up to 32 buttons to Gamepad A. */
    struct gamepad_report_t rep = {0};
    memcpy(rep.buttons, &buttons, sizeof(buttons));
    return sc_hid_report_gamepad_a_send(&rep);
}

esp_err_t sc_hid_report_gamepad_release(void)
{
    struct gamepad_report_t rep = {0};
    esp_err_t err1 = sc_hid_report_gamepad_a_send(&rep);
    esp_err_t err2 = sc_hid_report_gamepad_b_send(&rep);
    return (err1 == ESP_OK) ? err2 : err1;
}

esp_err_t sc_hid_report_consumer_send(uint16_t usage)
{
    if (!tud_hid_ready()) return ESP_ERR_INVALID_STATE;
    tud_hid_report(3, &usage, sizeof(usage));
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
    struct gamepad_report_t current_a = {0};
    struct gamepad_report_t current_b = {0};
    uint32_t button_release_time[256] = {0};

    uint16_t current_consumer = 0;
    uint32_t consumer_release_time = 0;

    struct gamepad_report_t last_sent_a;
    struct gamepad_report_t last_sent_b;
    memset(&last_sent_a, 0xFF, sizeof(last_sent_a)); /* force initial send */
    memset(&last_sent_b, 0xFF, sizeof(last_sent_b));

    uint16_t last_sent_consumer = 0xFFFF;

    hid_dispatch_t msg;

    for (;;) {
        uint32_t now = esp_log_timestamp();

        /* Check auto-releases for all 256 gamepad buttons */
        for (int i = 0; i < 256; i++) {
            if (button_release_time[i] > 0 && now >= button_release_time[i]) {
                if (i < 128) {
                    current_a.buttons[i / 8] &= ~(1 << (i % 8));
                } else {
                    int idx = i - 128;
                    current_b.buttons[idx / 8] &= ~(1 << (idx % 8));
                }
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
                if (msg.action.gamepad_button > 0 && msg.action.gamepad_button <= 256) {
                    int i = msg.action.gamepad_button - 1;
                    if (i < 128) {
                        current_a.buttons[i / 8] |= (1 << (i % 8));
                    } else {
                        int idx = i - 128;
                        current_b.buttons[idx / 8] |= (1 << (idx % 8));
                    }
                    button_release_time[i] = 0; /* cancel any active auto-release */
                } else if (msg.action.consumer_usage > 0) {
                    current_consumer = msg.action.consumer_usage;
                    consumer_release_time = 0;
                }
            } else if (msg.cmd == HID_CMD_RELEASE) {
                if (msg.action.gamepad_button > 0 && msg.action.gamepad_button <= 256) {
                    int i = msg.action.gamepad_button - 1;
                    if (i < 128) {
                        current_a.buttons[i / 8] &= ~(1 << (i % 8));
                    } else {
                        int idx = i - 128;
                        current_b.buttons[idx / 8] &= ~(1 << (idx % 8));
                    }
                    button_release_time[i] = 0;
                } else if (msg.action.consumer_usage > 0) {
                    if (current_consumer == msg.action.consumer_usage) {
                        current_consumer = 0;
                    }
                    consumer_release_time = 0;
                }
            } else if (msg.cmd == HID_CMD_PULSE) {
                uint32_t hold = msg.hold_ms > 0 ? msg.hold_ms : 50;
                if (msg.action.gamepad_button > 0 && msg.action.gamepad_button <= 256) {
                    int i = msg.action.gamepad_button - 1;
                    if (i < 128) {
                        current_a.buttons[i / 8] |= (1 << (i % 8));
                    } else {
                        int idx = i - 128;
                        current_b.buttons[idx / 8] |= (1 << (idx % 8));
                    }
                    button_release_time[i] = now + hold;
                } else if (msg.action.consumer_usage > 0) {
                    current_consumer = msg.action.consumer_usage;
                    consumer_release_time = now + hold;
                }
            }
        }

        /* Send reports on changes */
        if (tud_hid_ready()) {
            if (memcmp(&current_a, &last_sent_a, sizeof(struct gamepad_report_t)) != 0) {
                if (sc_hid_report_gamepad_a_send(&current_a) == ESP_OK) {
                    last_sent_a = current_a;
                }
            }
            if (memcmp(&current_b, &last_sent_b, sizeof(struct gamepad_report_t)) != 0) {
                if (sc_hid_report_gamepad_b_send(&current_b) == ESP_OK) {
                    last_sent_b = current_b;
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
#if CONFIG_IDF_TARGET_ESP32P4
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
#else
    ESP_LOGI(TAG, "Native USB PHY is fixed on this target; skipping phy swap.");
#endif
}
