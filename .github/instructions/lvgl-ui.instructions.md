---
description: "Use when writing or reviewing LVGL 9 UI code in the sc_ui component. Covers lv_lock guards, screen lifecycle, widget patterns, touch input routing, and PSRAM-safe buffer allocation for the 800x1280 MIPI-DSI display."
applyTo: "components/sc_ui/**"
---

# LVGL 9 UI Conventions (sc_ui component)

## Display Specs
- Resolution: 800 × 1280 px (portrait)
- Color depth: 16-bit RGB565
- Touch: Goodix GT911 (I²C), 5-point multitouch
- Driver: MIPI-DSI via `esp_lcd` panel API

## Thread Safety — MANDATORY
**Every LVGL call must be wrapped in lock/unlock:**
```c
lv_lock();
lv_label_set_text(label, "Armed");
lv_unlock();
```
Failure to lock will cause random crashes. There are no exceptions.

## Screen Lifecycle
```c
// Each screen is a .c/.h pair in components/sc_ui/screens/
// Implement these four functions:
lv_obj_t *sc_ui_screen_<name>_create(lv_obj_t *parent);
void      sc_ui_screen_<name>_load(const sc_terminal_config_t *cfg);
void      sc_ui_screen_<name>_destroy(void);
void      sc_ui_screen_<name>_on_event(sc_gamelink_event_t *evt);
```

## Widget Patterns
```c
// Button that fires a HID action on release:
lv_obj_t *btn = lv_button_create(parent);
lv_obj_set_size(btn, 120, 60);
lv_obj_add_event_cb(btn, sc_ui_btn_released_cb, LV_EVENT_RELEASED, action_ptr);

// Status indicator (label + color):
lv_obj_t *lbl = lv_label_create(parent);
lv_label_set_text(lbl, "SAFE");
lv_obj_set_style_text_color(lbl, lv_color_hex(0x00FF88), 0);
```

## Colour Palette
Use these named colours for consistency across all ship consoles:
| Name | Hex | Meaning |
|---|---|---|
| SC_COL_ARMED | `0xFF4444` | Weapon armed / danger |
| SC_COL_READY | `0x00FF88` | System online / safe |
| SC_COL_WARN  | `0xFFAA00` | Caution / degraded |
| SC_COL_OFF   | `0x333333` | System offline |
| SC_COL_ACCENT| `0x0099FF` | UI highlight / selected |

Define these in `components/sc_ui/include/sc_ui_theme.h`.

## Buffer Allocation
Frame buffers must live in PSRAM:
```c
// Two-buffer DMA for MIPI-DSI
static uint8_t *s_fb0;
static uint8_t *s_fb1;
s_fb0 = heap_caps_malloc(SC_UI_FB_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
s_fb1 = heap_caps_malloc(SC_UI_FB_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
assert(s_fb0 && s_fb1); // hard failure if PSRAM is exhausted
```

## Navigation / Screen Router
- `sc_ui_router_push(screen_id)` — push a screen onto the stack
- `sc_ui_router_pop()` — return to previous screen
- `sc_ui_router_home()` — jump to the ship console root
- Screen IDs are defined in `sc_ui_screens.h` as an enum
