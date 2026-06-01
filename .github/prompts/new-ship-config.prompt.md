---
name: "New Ship Config"
description: "Generate a complete ship JSON config file for a new Star Citizen ship. Use when adding a new ship with all its player positions, consoles, and initial action bindings."
argument-hint: "ship name e.g. 'Constellation Andromeda'"
agent: "agent"
tools: ["codebase", "create_file", "web"]
---

# New Ship Configuration

You are creating a new ship definition for: **${input}**

## Steps

### 1. Determine ship metadata
Derive from the ship name:
- `ship_id` — lowercase_underscores version of the name
- `ship_name` — full official RSI name
- `manufacturer` — ship manufacturer (Drake, RSI, Aegis, MISC, etc.)

### 2. Identify player positions
Based on the ship type, define the crew positions present:
- **Fighter/Light** (1 seat): `pilot`
- **Medium multi-crew**: `pilot`, `copilot`
- **Large/Capital**: `pilot`, `copilot`, `gunner_*`, `engineer`, `captain`

Common position names: `pilot`, `copilot`, `gunner_top`, `gunner_belly`, `engineer`, `captain`, `operator`

### 3. Define consoles for each position
Each position typically has 2–4 consoles. Suggested baseline per position:

| Position | Consoles |
|---|---|
| pilot | `pilot_mfd_left`, `pilot_mfd_right`, `pilot_power`, `pilot_weapons` |
| copilot | `copilot_mfd_left`, `copilot_mfd_right` |
| gunner | `<pos>_fire_control`, `<pos>_targeting` |
| engineer | `engineer_power`, `engineer_damage_control` |

### 4. Add baseline actions per console
For each console, add at minimum these actions using grid layout `grid_4x5`:

**pilot_mfd_left** (row 0–4, col 0–3):
- `toggle_landing_gear` (Space, row 0 col 0)
- `toggle_shields` (row 0 col 1)
- `request_landing` (row 0 col 2)
- `toggle_vtol` (row 0 col 3) — if applicable

**pilot_weapons** baseline:
- `fire_primary`, `fire_secondary`, `toggle_missiles`, `cycle_target`

### 5. Create the file
Output path: `data/ships/<ship_id>.json`

Follow the exact schema from [.github/instructions/ship-config-schema.instructions.md](.github/instructions/ship-config-schema.instructions.md).

### 6. Validate
- All `action.id` values unique within each console
- Grid positions don't overlap
- `console_id` follows `<position>_<panel>_<side?>` convention
