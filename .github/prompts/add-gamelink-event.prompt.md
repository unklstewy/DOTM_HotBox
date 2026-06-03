---
name: "Add Gamelink Event"
description: "Add a new Star Citizen Game.log event handler to sc_gamelink. Use when a new in-game state change (gear deployed, shields down, weapon armed, jump engaged) needs to drive UI updates or HID feedback on the terminal."
argument-hint: "event name e.g. 'gear_state_changed'"
agent: "agent"
tools: ["codebase", "replace_string_in_file"]
---

# Add Game.log Event Handler

You are wiring a new `sc_gamelink` event for: **${input}**

Derive `<event_name>` as lowercase_underscores.

## Overview
The `sc_gamelink` component parses events pushed from the PC bridge over WebSocket.
Each event carries an `event_id` string and a JSON payload.
Registered handlers are called on the GameLink task (priority 6).

## 1. Add Event ID Constant
In `components/sc_gamelink/include/sc_gamelink.h`:
```c
#define SC_GAMELINK_EVT_<EVENT_NAME_UPPER>   "<event_name>"
```

## 2. Define/Extend Typed Payload in `sc_gamelink.h`
If this event has unique fields, add a dedicated payload type and union member:
```c
typedef struct {
    char state[16];    // e.g. "up" | "down" | "deploying"
} sc_gamelink_<event_name>_payload_t;

typedef struct {
    // ...
    union {
        // existing members ...
        sc_gamelink_<event_name>_payload_t <event_name>;
        char raw[SC_GAMELINK_MAX_PAYLOAD_LEN];
    } payload;
} sc_gamelink_event_t;
```

If the event shares an existing payload shape, reuse it.

Also update `components/sc_gamelink/sc_gamelink.c`:
- add a parser helper `sc_gamelink_parse_<event_name>()`
- call it from `sc_gamelink_dispatch()` in the event_id switch/if chain

## 3. Register a Handler
In the component or screen that cares about this event, call:
```c
sc_gamelink_handler_register(
    SC_GAMELINK_EVT_<EVENT_NAME_UPPER>,
    sc_<component>_on_<event_name>,
    user_data_ptr   // NULL if none
);
```

## 4. Implement the Handler
```c
static void sc_<component>_on_<event_name>(const sc_gamelink_event_t *evt, void *user_data)
{
    static const char *TAG = "sc_<component>";
    ESP_LOGD(TAG, "<event_name>: state=%s", evt->payload.<event_name>.state);

    // Example: update UI label
    lv_lock();
    lv_label_set_text(s_status_label, evt->payload.<event_name>.state);
    lv_unlock();
}
```

## 5. Update the PC Bridge
Open `tools/pc_bridge/event_parser.py` and add the new event to the parser:
```python
# In parse_log_line():
elif "GearStateChanged" in line:          # adjust log keyword
    return _emit("gear_state_changed", {"state": extract_gear_state(line)})
```

## 6. Add to ship JSON (optional)
If UI buttons react to this state, set `gamelink_event` in the relevant action in `data/ships/<ship_id>.json`:
```json
"state": {
  "gamelink_event": "<event_name>",
  "values": {
    "up":   { "label": "UP",   "color": "#00FF88" },
    "down": { "label": "DOWN", "color": "#FF4444" }
  }
}
```

## Constraints
- Handlers run on GameLink task (priority 6) — keep them short
- LVGL calls inside the handler MUST use lv_lock() / lv_unlock()
- Never block inside a handler (no I²C, no NVS writes)
