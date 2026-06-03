# Drake Military — Battle-Worn (concept pass 1)

Source-of-truth SVG artwork for visual style approval. **No firmware code depends on
these files yet.** Open the `.svg` files directly in a web browser.

## Palette

| Token         | Hex       | Use                                                   |
|---------------|-----------|-------------------------------------------------------|
| `bg-void`     | `#000000` | True black — primary background, suppresses bleed.    |
| `bg-deck`     | `#0B0807` | Panel fill, almost-black warm.                        |
| `bg-recess`   | `#140E0A` | Recessed wells / inactive surfaces.                   |
| `chrome`      | `#2A1F18` | Steel/painted bezel base.                             |
| `chrome-hi`   | `#5A4636` | Worn metal edge highlight.                            |
| `amber`       | `#FFB000` | Primary accent — nominal data, lit segments.          |
| `amber-dim`   | `#7A5400` | Unlit / inactive amber.                               |
| `rust`        | `#8A3B12` | Faction trim, weathered paint.                        |
| `rust-dark`   | `#3A1808` | Shadow of rust trim.                                  |
| `warn-red`    | `#FF1F1F` | Hard warnings, weapons hot, damage.                   |
| `cool-cyan`   | `#3FB6C8` | Coolant / shields (cool-side data only).              |
| `bone`        | `#C8B89C` | Stencil text, off-white labels.                       |
| `bone-dim`    | `#6A5E4E` | Secondary labels.                                     |

## Files

- `sprite_sheet.svg` — every widget in the kit on one labelled canvas (1280×1600).
- `mockup_pilot_mfd.svg` — 800×1280 portrait, pilot left MFD.
- `mockup_engineering.svg` — 800×1280 portrait, engineering / power management.

## Style notes

- Drake = utilitarian, riveted, hand-painted stencils. Hazard tape on edges, chipped
  paint at corners, faint scuffs across flat panels.
- Type is monospaced and pixel-aligned. No anti-aliased gradients on data text.
- Glow is fake-bloom: 1–2 px offset duplicate stroke at lower alpha. No real blur on
  small text (the runtime PPA can't cheaply blur per frame).
- Backgrounds are dark warm, not neutral. The whole UI reads "diesel + ozone."
