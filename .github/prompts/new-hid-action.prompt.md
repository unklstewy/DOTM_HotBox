---
name: "New HID Action"
description: "Scaffold a new USB HID action binding in a ship JSON config and wire it up to a UI button in sc_ui. Use when adding a new cockpit control, keybinding, or MFD button to a ship console."
argument-hint: "action label e.g. 'Toggle Landing Gear'"
agent: "agent"
tools: ["codebase", "create_file", "replace_string_in_file"]
---

# New HID Action Scaffold

You are adding a new HID action to the SC Terminal project. The argument is: **${input}**

## Steps

### 1. Locate the target ship JSON
Search `data/ships/` for the relevant ship JSON. Ask the user which ship and console if not specified.

### 2. Add the action entry
Following the schema in [.github/instructions/ship-config-schema.instructions.md](.github/instructions/ship-config-schema.instructions.md), append a new action object to the correct console's `actions` array.

Required fields:
- `id` — unique snake_case identifier
- `label` — short uppercase display label (max 8 chars)
- `description` — human-readable
- `icon` — icon name (use `"button"` as default)
- `row`, `col`, `width`, `height` — grid position (ask user or pick next available)
- `hid.modifier`, `hid.keycodes`, `hid.consumer_usage`, `hid.hold_ms`
- `state` — set `gamelink_event: null` if no game state feedback

### 3. Verify no duplicate `id` in the console
Scan the JSON actions array — `id` must be unique within the console.

### 4. Report what was added
Show the action object that was inserted and its grid position.

## Key Constraints (from [.github/instructions/sc-hid.instructions.md](.github/instructions/sc-hid.instructions.md))
- Never hard-code keycodes in C — they only live in the JSON
- EAC-safe: HID output only, no OS injection
- `hold_ms: 0` = tap, `hold_ms: >0` = held key
