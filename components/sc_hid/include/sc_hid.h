/*
 * sc_hid.h — Public API for the USB HID subsystem
 *
 * Manages a TinyUSB composite HID device with two reports:
 *   Report 1 — Boot Keyboard (8-byte: modifier + 6 keycodes)
 *   Report 2 — Consumer Control (2-byte usage code)
 *
 * All public functions are thread-safe. The HID task runs at
 * priority SC_HID_TASK_PRIORITY (10) to guarantee sub-2ms latency.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ──────────────────────────────────────────────────────────── */
#define SC_HID_MAX_ACTIONS          (128)
#define SC_HID_ACTION_ID_LEN        (32)
#define SC_HID_TASK_STACK_SIZE      (4096)
#define SC_HID_TASK_PRIORITY        (10)

/* ── Types ──────────────────────────────────────────────────────────────── */

/** One HID action loaded from the ship JSON config. */
typedef struct {
    char     action_id[SC_HID_ACTION_ID_LEN]; /**< Matches JSON "id" field   */
    uint8_t  gamepad_button;                   /**< Gamepad button (1-32, 0=none) */
    uint16_t consumer_usage;                   /**< Consumer usage (0 = none)  */
    uint32_t hold_ms;                          /**< 0 = tap, >0 = hold         */
} sc_hid_action_t;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/**
 * @brief Initialise TinyUSB HID device and start the HID task.
 *        Called once from app_main after sc_config_init().
 */
esp_err_t sc_hid_init(void);

/** @brief Shut down HID device and delete task. */
void sc_hid_deinit(void);

/** @brief Enable or disable the USB PHY software override to swap physical port to OTG_FS. */
void sc_hid_set_phy_swap(bool enable);

/* ── Action Table ───────────────────────────────────────────────────────── */

/**
 * @brief Load action table from an array (parsed from ship JSON).
 *        Replaces any existing table.
 * @param actions  Pointer to array of sc_hid_action_t structs.
 * @param count    Number of entries in the array.
 */
esp_err_t sc_hid_action_table_load(const sc_hid_action_t *actions,
                                   size_t                  count);

/* ── Action Dispatch ────────────────────────────────────────────────────── */

/**
 * @brief Send a tap (press + release) for the named action.
 * @param action_id  Must match an entry in the loaded action table.
 */
esp_err_t sc_hid_action_send(const char *action_id);

/**
 * @brief Hold the keys for the named action then release.
 * @param action_id    Action to hold.
 * @param override_ms  Duration override; 0 = use value from action table.
 */
esp_err_t sc_hid_action_hold(const char *action_id, uint32_t override_ms);

/**
 * @brief Set the gamepad button or consumer control state to pressed (held).
 * @param action_id Action to press.
 */
esp_err_t sc_hid_action_press(const char *action_id);

/**
 * @brief Set the gamepad button or consumer control state to released.
 * @param action_id Action to release.
 */
esp_err_t sc_hid_action_release(const char *action_id);

/* ── Low-level Report API (internal / testing only) ────────────────────── */

/** @brief Send a raw gamepad report. */
esp_err_t sc_hid_report_gamepad_send(uint32_t buttons);

/** @brief Release all gamepad buttons. */
esp_err_t sc_hid_report_gamepad_release(void);

/** @brief Send a raw consumer control report. */
esp_err_t sc_hid_report_consumer_send(uint16_t usage);

#ifdef __cplusplus
}
#endif
