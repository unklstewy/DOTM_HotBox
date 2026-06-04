# Agent Instruction: Phase 2 - Gamepad & Media HID Profile

**Objective**: Refactor the USB HID component (`sc_hid`) from a Keyboard emulator to a Game Controller + Media Controller composite device.

## Context
Currently, the `sc_hid` component simulates keyboard strokes (e.g., sending 'A', 'B', 'Space'). This disrupts the host operating system, especially when a game window is out of focus. A Game Controller (Gamepad/Joystick) device allows games like Star Citizen to map buttons natively without typing letters into OS text fields. We also want to support Media Controller endpoints (Play/Pause, Volume Up/Down).

## Execution Steps

1. **Rewrite the HID Report Descriptor**
   - Open `components/sc_hid/sc_hid.c`.
   - Replace the `hid_report_descriptor` array with a new descriptor that specifies a Gamepad (Usage Page 0x01, Usage 0x05).
   - Configure at least 32 discrete buttons, an X/Y axis (if joystick support is desired), and a separate top-level collection for a Consumer Control device (Usage Page 0x0C, Usage 0x01) for media keys.
   - Adjust `TUSB_DESC_TOTAL_LEN` and related TinyUSB descriptor constants as necessary.

2. **Update the HID API**
   - Change the signature of `sc_hid_send_keypress` to `sc_hid_send_gamepad_button(uint8_t button_id, bool pressed)`.
   - Implement `sc_hid_send_media_key(uint16_t media_keycode)`.
   - Ensure the HID task updates the corresponding bitmask in the Gamepad report struct and triggers a TinyUSB report.

3. **Update `sc_gamelink` & `sc_ui` Mappings**
   - Update `sc_hid_action_t` to map to Gamepad buttons (1-32) rather than ASCII character codes.
   - Refactor `sc_ui` console logic so that touching a screen widget emits a Gamepad button event over `sc_gamelink`, which `sc_hid` then consumes.
   - Ensure the JSON action tables (loaded by `sc_config`) map string actions to the new Gamepad indices.

4. **Verify**
   - Compile locally with `idf.py build`.
   - Ensure TinyUSB mounts as a Gamepad and Consumer Control device on the host.
