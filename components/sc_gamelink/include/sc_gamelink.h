/*
 * sc_gamelink.h — Star Citizen Game.log event consumer
 *
 * Receives JSON event frames from the PC bridge via sc_network WebSocket,
 * parses them, and dispatches to registered per-event handlers.
 *
 * Event frame format (WebSocket text message):
 * {
 *   "event": "gear_state_changed",
 *   "ship":  "cutlass_black",
 *   "ts":    1234567890,
 *   "data":  { ... event-specific fields ... }
 * }
 *
 * Task priority: SC_GAMELINK_TASK_PRIORITY (6)
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ──────────────────────────────────────────────────────────── */
#define SC_GAMELINK_TASK_STACK_SIZE     (4096)
#define SC_GAMELINK_TASK_PRIORITY       (6)
#define SC_GAMELINK_MAX_HANDLERS        (32)
#define SC_GAMELINK_EVENT_ID_LEN        (48)
#define SC_GAMELINK_MAX_PAYLOAD_LEN     (256)

/* ── Known Event IDs ────────────────────────────────────────────────────── */
#define SC_GAMELINK_EVT_GEAR_STATE      "gear_state_changed"
#define SC_GAMELINK_EVT_SHIELD_STATE    "shield_state_changed"
#define SC_GAMELINK_EVT_POWER_STATE     "power_state_changed"
#define SC_GAMELINK_EVT_WEAPON_ARMED    "weapon_armed_changed"
#define SC_GAMELINK_EVT_QUANTUM_READY   "quantum_drive_ready"
#define SC_GAMELINK_EVT_QUANTUM_STATE   "quantum_drive_state"
#define SC_GAMELINK_EVT_SHIP_SPAWN      "ship_spawned"
#define SC_GAMELINK_EVT_SHIP_DESTROYED  "ship_destroyed"
#define SC_GAMELINK_EVT_PLAYER_RESPAWN  "player_respawned"

/* ── Event Types ────────────────────────────────────────────────────────── */

/** Payload for gear_state_changed */
typedef struct {
    char state[16];   /**< "up" | "down" | "deploying" | "retracting" */
} sc_gamelink_gear_payload_t;

/** Payload for shield_state_changed */
typedef struct {
    float front_pct;  /**< 0.0–1.0 */
    float back_pct;
    float left_pct;
    float right_pct;
} sc_gamelink_shield_payload_t;

/** Payload for quantum_drive_state */
typedef struct {
    char state[24];   /**< "idle" | "spooling" | "ready" | "jumping" | "cooldown" */
    float spool_pct;  /**< 0.0–1.0 */
} sc_gamelink_quantum_payload_t;

/** Generic event container */
typedef struct {
    char    event_id[SC_GAMELINK_EVENT_ID_LEN];
    char    ship_id[32];
    int64_t timestamp_ms;
    union {
        sc_gamelink_gear_payload_t    gear;
        sc_gamelink_shield_payload_t  shield;
        sc_gamelink_quantum_payload_t quantum;
        char raw[SC_GAMELINK_MAX_PAYLOAD_LEN]; /**< Fallback for unknown events */
    } payload;
} sc_gamelink_event_t;

/* ── Handler Type ───────────────────────────────────────────────────────── */
typedef void (*sc_gamelink_handler_fn_t)(const sc_gamelink_event_t *evt,
                                         void *user_ctx);

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/** @brief Initialise gamelink, register WS callback in sc_network, start task. */
esp_err_t sc_gamelink_init(void);

/** @brief Shut down gamelink task and deregister callbacks. */
void sc_gamelink_deinit(void);

/* ── Handler Registration ───────────────────────────────────────────────── */

/**
 * @brief Register a handler for a specific event ID.
 * @param event_id  Event identifier string (use SC_GAMELINK_EVT_* constants).
 * @param handler   Function to call when the event arrives.
 * @param user_ctx  Opaque pointer passed to handler (may be NULL).
 */
esp_err_t sc_gamelink_handler_register(const char             *event_id,
                                       sc_gamelink_handler_fn_t handler,
                                       void                   *user_ctx);

/**
 * @brief Deregister a previously registered handler.
 */
esp_err_t sc_gamelink_handler_deregister(const char             *event_id,
                                         sc_gamelink_handler_fn_t handler);

#ifdef __cplusplus
}
#endif
