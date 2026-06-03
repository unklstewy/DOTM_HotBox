# Origin Lux — Concept Pass 1

Source-of-truth SVG artwork for visual style approval — matched to the **Origin
Jumpworks 380 Jump** aesthetic: luxury touring yacht meets executive avionics.
**No firmware code depends on these files yet.**

Open the `.svg` files in any browser.

## Design language

- **Material:** smoked glass, brushed aluminium, anodised gold trim, hairline
  bezels. No rivets, no chipped paint, no hazard tape. Everything is precision-
  milled.
- **Light:** soft inner glow, 1 px top-edge highlight, faint shadow drop. Data
  reads as projected light, not painted graphics.
- **Geometry:** rounded corners (4–6 px), long thin proportions, generous
  negative space. Concentric rings instead of square frames where possible.
- **Typography:** monospaced but light — wide tracking, low weight for labels,
  one bold weight reserved for primary numbers. Title case allowed (Drake was
  all-caps stencil; Origin is "Title Case · with center dots").
- **Motion (future):** slow 1 Hz scanlines, gentle pulsing, no flicker. Origin
  UIs feel calm even in combat.

## Palette

| Token        | Hex       | Use                                                |
|--------------|-----------|----------------------------------------------------|
| `bg-night`   | `#04070D` | Primary background — deep navy-near-black.         |
| `bg-glass`   | `#0A1320` | Panel fill (frosted glass).                        |
| `bg-glass-2` | `#11203A` | Recessed wells / hovered surfaces.                 |
| `bezel`      | `#243349` | Hairline panel edge.                               |
| `bezel-hi`   | `#5F7A9E` | Bright top-edge of glass.                          |
| `ice`        | `#6EC4FF` | Primary accent — data, lit elements.               |
| `ice-bright` | `#BFE5FF` | Highlight / focus / pulse peak.                    |
| `ice-dim`    | `#2A6B96` | Inactive ice.                                      |
| `gold`       | `#D4B26A` | Luxury trim, capacity rings, brand mark.           |
| `gold-dim`   | `#6E5A33` | Inactive gold.                                     |
| `warn-amber` | `#FFB454` | Soft warning (NOT red).                            |
| `warn-red`   | `#FF5A6A` | Hard warning (rose-red, not blood-red).            |
| `paper`      | `#E8EEF6` | Pure light text / glass numerals.                  |
| `paper-dim`  | `#7C8AA0` | Secondary labels.                                  |

## Contrast with Drake

| | Drake Military | Origin Lux |
|---|---|---|
| Mood | grimy, riveted | polished, calm |
| Accent | amber + rust | ice blue + gold |
| Corners | chipped 45° | rounded 4–6 px |
| Type | stenciled CAPS | spaced Title Case |
| Surface | painted steel | smoked glass |
| Wear | scuffs + chips | hairline reflections only |
| Hazard cue | yellow tape | thin rose-red underline |

## Files

- `sprite_sheet.svg` — full UI kit on one labelled canvas.
- `mockup_nav_cruise.svg` — 800×1280 portrait, 380 Jump nav / cruise console
  (waypoint planning, quantum spool, comfort telemetry).
