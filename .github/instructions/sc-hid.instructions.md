---
description: "Use when creating or modifying USB HID descriptors, HID action tables, or TinyUSB code in sc_hid. Covers composite keyboard+consumer HID descriptors, action dispatch, keycode mapping, and EAC-safe output rules."
applyTo: "components/sc_hid/**"
---

# USB HID (sc_hid) Conventions

## Device Configuration
- **Composite device**: Keyboard (Boot protocol) + Consumer Control (media/sim keys)
- Driver: TinyUSB via `idf_component_manager` (`idf/tinyusb`)
- VID/PID: Defined in `components/sc_hid/include/sc_hid_desc.h` — never hard-code elsewhere
- Endpoint buffer: 64 bytes (HID FS)

## HID Descriptor Rules
- Keyboard report: 8-byte Boot Keyboard (modifier + 6 keycodes)
- Consumer report: 2-byte Consumer Control (single usage code)
- Both reports share the same interface; use separate Report IDs (1 = keyboard, 2 = consumer)

## Action Table
Actions are **never** hard-coded in UI code. They come from the ship JSON:
```c
// sc_hid_action_t is populated from JSON at runtime:
typedef struct {
    char       action_id[32];   // matches JSON "id" field
    uint8_t    modifier;        // HID modifier byte (Ctrl/Shift/Alt/GUI)
    uint8_t    keycodes[6];     // up to 6 simultaneous keys
    uint16_t   consumer_usage;  // consumer usage code (0 = none)
    uint32_t   hold_ms;         // 0 = tap, >0 = hold duration
} sc_hid_action_t;
```

## Sending an Action
```c
// Tap a key:
esp_err_t sc_hid_action_send(const char *action_id);

// Hold for duration then release:
esp_err_t sc_hid_action_hold(const char *action_id, uint32_t override_ms);

// Direct report (internal use only, not called from UI):
esp_err_t sc_hid_report_keyboard_send(uint8_t mod, const uint8_t keycodes[6]);
esp_err_t sc_hid_report_consumer_send(uint16_t usage);
```

## EAC Safety Rules
- Output HID reports only — never call OS APIs (SendInput, WriteFile to device) on the PC side
- HID reports go over the USB cable directly from ESP32-P4 to the PC
- Do not read PC memory, game memory, or any process state
- The PC bridge only reads Game.log (a plain text file) — read-only, no API hooking

## Key Codes
Use the `tusb_hid.h` constants:
- `HID_KEY_A` through `HID_KEY_Z`
- `HID_KEY_F1` through `HID_KEY_F24`
- `HID_KEY_SPACE`, `HID_KEY_ENTER`, `HID_KEY_ESCAPE`
- Modifier masks: `KEYBOARD_MODIFIER_LEFTCTRL`, `_LEFTSHIFT`, `_LEFTALT`, `_LEFTGUI`
- Consumer: `HID_USAGE_CONSUMER_PLAY_PAUSE`, custom sim codes via vendor usage page

## Task Priority
HID task runs at priority **10** (highest in the system) to ensure sub-2ms latency from button press to USB report.
