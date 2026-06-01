---
description: "Use when working on ship JSON config files in data/ships/, the sc_config component, or adding new ships/consoles/terminal layouts. Covers JSON schema, console_id naming, action binding format, and multi-terminal layout rules."
applyTo: "data/ships/**"
---

# Ship Configuration Schema

## File Location
```
data/ships/<ship_id>.json
```
`ship_id` must be lowercase with underscores: `cutlass_black`, `idris_m`, `constellation_andromeda`.

## Top-Level Schema
```jsonc
{
  "ship_id": "cutlass_black",
  "ship_name": "Drake Cutlass Black",
  "manufacturer": "Drake Interplanetary",
  "consoles": [ /* array of console objects */ ]
}
```

## Console Object
```jsonc
{
  "console_id": "pilot_mfd_left",
  "display_name": "Pilot MFD — Left",
  "position": "pilot",          // pilot | copilot | gunner | engineer | captain
  "layout": "grid_4x5",         // grid_WxH or custom
  "actions": [ /* array of action objects */ ]
}
```

### `console_id` Naming Convention
`<position>_<panel>_<side?>`
Examples: `pilot_mfd_left`, `pilot_mfd_right`, `pilot_power`, `gunner_weapons_primary`

## Action Object
```jsonc
{
  "id": "toggle_landing_gear",
  "label": "GEAR",
  "description": "Toggle landing gear up/down",
  "icon": "gear",               // icon name from sc_ui icon set
  "row": 0,
  "col": 0,
  "width": 1,                   // grid cells wide
  "height": 1,                  // grid cells tall
  "hid": {
    "modifier": 0,              // HID modifier byte (0 = none)
    "keycodes": [44],           // [Space] — use decimal HID key codes
    "consumer_usage": 0,        // 0 = keyboard, non-zero = consumer control
    "hold_ms": 0                // 0 = tap
  },
  "state": {
    "gamelink_event": "gear_state_changed",  // null if no state feedback
    "values": {
      "up":   { "label": "GEAR UP",   "color": "#00FF88" },
      "down": { "label": "GEAR DOWN", "color": "#FF4444" }
    }
  }
}
```

## Multi-Terminal Layout (Cutlass Black Example)
The Cutlass Black pilot position has 4 terminals:
```
console_id               terminal_index
──────────────────────   ──────────────
pilot_mfd_left           0
pilot_mfd_right          1
pilot_power              2
pilot_weapons            3
```
Each physical reTerminal device is flashed with the same firmware but stores its `console_id` and `terminal_index` in NVS. The UI reads these at boot to load the correct console layout.

## Validation Rules
- Every `action.id` must be unique within a console
- `row` + `height` must not exceed the layout grid height
- `col` + `width` must not exceed the layout grid width
- `gamelink_event` strings must match events defined in `sc_gamelink`
