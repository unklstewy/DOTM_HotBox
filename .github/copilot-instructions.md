# SC Terminal — Project Copilot Instructions

## Project Identity
This is an ESP-IDF (v5.3+) firmware project for the **Seeed Studio reTerminal D1001** (ESP32-P4NRW32 + ESP32-C6 coprocessor). It is a **Star Citizen physical HID dashboard terminal** — it renders in-game ship console UIs on an 8" touch screen and sends USB HID keystrokes to the simulation PC.

## Hardware Facts (never guess these)
| Item | Value |
|---|---|
| MCU | ESP32-P4NRW32, dual-core RISC-V @ 400 MHz |
| PSRAM | 32 MB OCT on-package |
| Flash | 32 MB SPI |
| Display | 8" 800×1280 MIPI-DSI, Goodix GT911 capacitive touch |
| Wireless | ESP32-C6 coprocessor (Wi-Fi 6 / BLE 5) via `esp_hosted` |
| USB | USB-C with OTG — **native TinyUSB HID** on ESP32-P4 |
| Storage | microSD + SPIFFS partition for ship JSON assets |
| IDF target | `esp32p4` |

## Architecture — Component Map
```
main/            — boot sequencer only
components/
  sc_config/     — NVS + SPIFFS: ship & console layout persistence
  sc_hid/        — TinyUSB composite HID (keyboard + consumer control)
  sc_ui/         — LVGL 9 display driver, screen router, widget library
  sc_network/    — Wi-Fi (via esp_hosted), mDNS, WebSocket client
  sc_gamelink/   — Game.log event parser (subscribed via WebSocket from PC bridge)
tools/
  pc_bridge/     — Python: game.log tail → WebSocket server → terminal push
data/
  ships/         — JSON ship definitions (controls, bindings, console layouts)
.github/
  instructions/  — Per-domain Copilot instructions (auto-attached by glob)
  prompts/       — Atomic slash-command prompts for scaffolding tasks
```

## Coding Conventions
- **Language**: C11 for all firmware; Python 3.12 for PC tools.
- **Naming**: `sc_<component>_<noun>_<verb>()` e.g. `sc_hid_action_send()`.
- **Error handling**: Always return `esp_err_t`; use `ESP_ERROR_CHECK()` at call sites.
- **Logging**: Use `ESP_LOGx(TAG, ...)` — define `static const char *TAG` per file.
- **FreeRTOS tasks**: Stack in PSRAM (`pvPortMallocCaps`). Priority band: UI=5, HID=10, Network=7, GameLink=6.
- **LVGL**: All LVGL calls **must** be made inside `lv_lock()` / `lv_unlock()` guards.
- **HID actions**: Defined in `data/ships/<ship_id>.json` — never hard-code keycodes in UI code.
- **Security**: No credentials in source. Wi-Fi credentials from NVS only. No memory scanning of the game process.

## Star Citizen Integration Rules
- Data source: `Game.log` file tailing via PC bridge (read-only, EAC-safe).
- Ship definitions: RSI public JSON / manual entry — no private API calls.
- HID output only — no input injection via OS APIs that could trigger EAC.
- Each terminal maps to one `console_id` within a `ship_id` (e.g. `cutlass_black/pilot_mfd_left`).

## Key Files to Know
- [main/main.c](main/main.c) — boot sequence
- [main/idf_component.yml](main/idf_component.yml) — managed dependencies
- [partitions.csv](partitions.csv) — 32 MB flash layout
- [sdkconfig.defaults](sdkconfig.defaults) — build-time Kconfig overrides
