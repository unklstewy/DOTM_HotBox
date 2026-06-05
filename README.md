# DOTM HotBox — Star Citizen Terminal

> A purpose-built touchscreen MFD (Multi-Function Display) for Star Citizen, running on the Seeed Studio reTerminal D1001. Appears to your PC as a USB gamepad, sends button presses as game-bound actions, and can react to in-game events via a companion PC bridge.

![SC Terminal — Pilot MFD]([sdcard/assets/images/splash_base_landscape.png))

---

## Contents

- [Overview](#overview)
- [Hardware](#hardware)
  - [Where to Buy](#where-to-buy)
  - [Specifications](#specifications)
- [Architecture](#architecture)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Build & Flash](#build--flash)
  - [First Boot & Configuration](#first-boot--configuration)
  - [SD Card Layout](#sd-card-layout)
- [Ship Profiles](#ship-profiles)
- [Themes](#themes)
- [PC Bridge (sc-bridge)](#pc-bridge-sc-bridge)
- [Web Portal](#web-portal)
- [Contributing](#contributing)

---

## Overview

The **DOTM HotBox** is a standalone touchscreen controller that sits next to your HOTAS/keyboard and gives you a ship-specific control panel for Star Citizen. Each button maps to a gamepad input that Star Citizen reads through its normal control binding system — no macros, no injection, no risk.

Key features:

- **Ship-aware layouts** — load a JSON profile per ship; the display automatically shows the right consoles and labels
- **USB HID gamepad** — appears as a standard gamepad (up to 32 buttons); bind in Star Citizen like any other controller
- **Live game state** (optional) — connect the PC bridge to receive `Game.log` events and light up buttons reactively (e.g., shield state, power preset, weapon mode)
- **Multi-console tabs** — swipe between MFD pages (Pilot MFD, Weapons, Power, Co-pilot) without losing context
- **Theme skins** — Default, Drake Military, and Origin Luxury panel art loaded from the SD card
- **Web Portal** — configure Wi-Fi, ship profile, and touch calibration from any browser on your local network
- **OTA-ready** — dual OTA partition table; flash new firmware over USB or remotely

### HotBox Lite (Headless Mode)

For hardware targets without a built-in LCD screen (such as ESP32-S3 or ESP32-C3 DevKits), **HotBox Lite** runs headlessly on the micro-controller and hosts the interactive control canvas directly in the browser.

Any device on your local network (iPad, iPhone, Android tablet, Surface Pro, or a secondary desktop browser window) can open the Web Portal's **Play Mode** to serve as the physical control panel. Interactions on the browser screen are sent in real-time to the ESP32, which forwards them to your PC as standard USB gamepad reports.

---

## Hardware

### Where to Buy

| Item | Link |
|------|------|
| **Seeed Studio reTerminal D1001** | [seeedstudio.com/reTerminal-D1001](https://www.seeedstudio.com/reTerminal-D1001.html) |
| MicroSD card (recommended ≥ 16 GB, UHS-I) | Any reputable brand (Samsung, SanDisk) |

> HotBox Lite supports running headlessly on ESP32-S3 and ESP32-C3 dev kits, rendering the screen completely in a browser.

### Hardware Specifications

| Feature | Standard HotBox (reTerminal D1001) | HotBox Lite (ESP32-S3 DevKit) | HotBox Lite (ESP32-C3 DevKit-C1-8N16) |
|---|---|---|---|
| **Host SoC** | ESP32-P4NRW32 (400MHz Dual-Core RISC-V) | ESP32-S3 (240MHz Xtensa LX7) | ESP32-C3 (160MHz Single-Core RISC-V) |
| **Wi-Fi / BLE** | ESP32-C6 (via SDIO) | Native 2.4 GHz Wi-Fi + BLE 5.0 | Native 2.4 GHz Wi-Fi + BLE 5.0 |
| **SRAM** | 32 MB PSRAM + 768 KB SRAM | 512 KB SRAM + optional PSRAM | 400 KB SRAM (No PSRAM) |
| **Internal Flash** | 32 MB | 8 MB / 16 MB | 8 MB |
| **Display** | 5" DSI IPS (800x1280, JD9365) | Headless (rendered in browser) | Headless (rendered in browser) |
| **Touch Sensor** | GSL3670 (Capacitive) | Headless (pointer events in browser) | Headless (pointer events in browser) |
| **Storage** | MicroSD slot (SDMMC 4-bit) | SPIFFS Flash Partition (7.5 MB) | SPIFFS Flash Partition (7.5 MB) |
| **HID Interface** | Native USB OTG Gamepad Device | Native USB OTG Gamepad Device | Serial/WebSocket command forwarding |
| **Power Input** | USB-C 5 V (from PC) | USB-C 5 V (from PC) | USB-C 5 V (from PC) |

#### GPIO / Peripheral Map

| Signal | GPIO | Notes |
|--------|------|-------|
| SDMMC Slot 0 CLK | 43 | SD card — IOMUX fixed |
| SDMMC Slot 0 CMD | 44 | SD card — IOMUX fixed |
| SDMMC Slot 0 D0–D3 | 39–42 | SD card — 4-bit |
| SD Card Power Enable | 46 | Active high |
| SDMMC Slot 1 CLK | 11 | ESP32-C6 SDIO |
| SDMMC Slot 1 CMD | 6 | ESP32-C6 SDIO |
| SDMMC Slot 1 D0–D3 | 7–10 | ESP32-C6 SDIO |
| C6 Reset (CHIP_PU) | 13 | Active high |
| I²C SCL (touch + expander) | 43 | Shared bus |
| I²C SDA (touch + expander) | 44 | Shared bus |
| DSI lane | — | Dedicated hardware |

---

## Architecture

```
┌─────────────────────────────────────────┐
│             ESP32-P4 (Host)             │
│                                         │
│  sc_main ─── boot sequencer             │
│  sc_config ─ NVS + ship JSON loader     │
│  sc_storage ─ FAT/SD card mount         │
│  sc_hid ──── USB HID gamepad (TinyUSB)  │
│  sc_ui ───── LVGL 9 display engine      │
│  sc_network ─ Wi-Fi client + WebSocket  │
│  sc_web ──── HTTP config portal (port 80│
│  sc_gamelink ─ game event subscriber    │
│  sc_bsp ──── hardware abstraction layer │
└─────────────┬───────────────────────────┘
              │ SDIO (Slot 1)
┌─────────────▼───────────────────────────┐
│         ESP32-C6 (Wi-Fi coprocessor)    │
│         esp-hosted 2.x (slave)          │
└─────────────────────────────────────────┘
```

The ESP32-P4 hosts all application logic. The ESP32-C6 is a transparent Wi-Fi coprocessor managed by the `espressif__esp_hosted` component — it exposes the standard `esp_wifi` API to the host, so the application code never talks to the C6 directly.

---

## Getting Started

### Prerequisites

| Tool | Version | Install |
|------|---------|---------|
| ESP-IDF | **6.0.1** | [docs.espressif.com/get-started](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32p4/get-started/) |
| Python | 3.9 + | Bundled with ESP-IDF |
| `idf.py` | included | Sourced from ESP-IDF environment |

> **Important:** The project is pinned to ESP-IDF **6.0.1**. Other versions are not tested and may fail to build due to the `sd_host` driver API changes introduced in IDF 6.

```bash
# Source the ESP-IDF environment (adjust path to your install)
. $HOME/.espressif/v6.0.1/esp-idf/export.sh
```

### Build & Flash

First, clone the repository:
```bash
git clone https://github.com/yourname/ESPSCar.git
cd ESPSCar
```

#### Step 1: Compile the Web Portal Assets (Required for all targets)
The Web Portal UI must be built first so its assets can be bundled (into SPIFFS or served):
```bash
cd web_portal
npm install
npm run build
cd ..
# Copy the built portal assets to the SPIFFS build directory
cp -r web_portal/dist/* spiffs_image/web/
```

#### Step 2: Build and Flash by Target

##### Option A: Standard HotBox (reTerminal D1001 — ESP32-P4)
This is the default target. It compiles the LVGL display screen, SD card storage reader, and USB HID gamepad:
```bash
# Set target to esp32p4 and configure defaults
idf.py set-target esp32p4
idf.py reconfigure

# Compile and Flash
idf.py -p /dev/ttyACM0 flash monitor
```

##### Option B: HotBox Lite (Headless Mode — ESP32-S3)
Compiles a headless build with SPIFFS flash storage support and native USB OTG gamepad emulation:
```bash
# Set target to esp32s3
idf.py set-target esp32s3
idf.py reconfigure

# Compile and Flash (this compiles the SPIFFS storage binary and flashes it automatically)
idf.py -p /dev/ttyACM0 flash monitor
```

##### Option C: HotBox Lite (Headless Mode — ESP32-C3)
Compiles a headless build with SPIFFS flash storage. Gamepad commands are routed over Wi-Fi / WebSocket:
```bash
# Set target to esp32c3
idf.py set-target esp32c3
idf.py reconfigure

# Compile and Flash
idf.py -p /dev/ttyUSB0 flash monitor
```

On Linux, add yourself to the `dialout` group if you get a permission error:

```bash
sudo usermod -aG dialout $USER
# log out and back in
```

### First Boot & Configuration

#### Standard (with SD Card, e.g., reTerminal D1001)
1. **Insert a microSD card** formatted as FAT32 (exFAT is also supported on IDF 6).
2. **Copy the SD card contents** from the `sdcard/` directory in this repo to the root of the card.
3. Power on the device — the splash screen loads, then the boot menu appears.

#### HotBox Lite (Headless SPIFFS targets, e.g., ESP32-S3/C3)
1. The firmware uses the internal flash storage with **SPIFFS** instead of an SD card.
2. Web portal build files (`web_portal/dist`), ship configs (`spiffs_image/ships`), and lightweight SVG theme assets (`spiffs_image/assets/themes/.../sprite_sheet.svg`) are located in the `spiffs_image/` directory.
3. These are compiled and flashed directly into the `storage` partition in the project using:
   ```bash
   idf.py build flash
   ```
4. Power on the device — it boots headlessly.

#### Configuration & Binding
1. **Connect to the Web Portal:**
   - The device connects to Wi-Fi using credentials stored in NVS.
   - On first boot (no credentials saved), it falls back to a softAP — connect to `HotBox-XXXX` and browse to `192.168.4.1`.
   - Set your SSID, password, ship profile, and bridge hostname.
5. **Bind the gamepad in Star Citizen:**
   - The device enumerates as a USB gamepad with up to 32 buttons.
   - Open Star Citizen → Options → Keybindings → Find "HotBox" under Joystick/Gamepad.
   - Bind each button to the matching in-game action.

#### Setting Wi-Fi credentials via `idf.py monitor`

If you prefer the CLI over the web portal:

```
# In the serial monitor, press Ctrl+T then Ctrl+H for the IDF monitor help
# Or use the web portal after connecting via softAP
```

### SD Card Layout

```
/sdcard
├── ships/
│   ├── cutlass_black.json      ← Drake Cutlass Black profile
│   ├── drake_cutlass_red.json  ← Drake Cutlass Red profile
│   ├── avenger_titan.json
│   ├── misc_prospector.json
│   ├── corsair.json
│   └── editor_controls.json   ← Meta: editor mode layout
└── assets/
    └── themes/
        ├── drake/              ← Drake Military panel art (.bin)
        │   ├── TL.bin  TR.bin  BL.bin  BR.bin
        │   ├── EDGE_T_tile_X.bin  EDGE_B_tile_X.bin
        │   ├── EDGE_L.bin  EDGE_R.bin
        │   ├── CENTER_stretch.bin
        │   └── btn_idle.bin  btn_armed.bin  btn_active.bin
        └── origin/             ← Origin Luxury panel art (.bin)
            ├── TL.bin  TR.bin  BL.bin  BR.bin
            ├── EDGE__T.bin  EDGE__B.bin
            ├── CENTER.bin
            └── btn_idle.bin  btn_hover.bin  btn_active.bin
```

> Theme `.bin` files are raw LVGL image descriptors. Use the LVGL image converter (`lvgl/scripts/LVGLImage.py`) to produce them from PNG assets.

---

## Ship Profiles

Each ship profile is a JSON file on the SD card at `/sdcard/ships/<ship_id>.json`. The `ship_id` is set via the web portal and stored in NVS.

A profile defines one or more **consoles** (MFD pages). Each console has a list of **actions**:

```jsonc
{
  "ship_id": "cutlass_black",
  "display_name": "Drake Cutlass Black",
  "consoles": [
    {
      "console_id": "pilot_mfd_left",
      "display_name": "Pilot MFD — Left",
      "position": "pilot",
      "layout": "grid_4x5",
      "actions": [
        {
          "id": "landing_gear",
          "label": "GEAR",
          "icon": "gear",
          "row": 0, "col": 0, "width": 2, "height": 1,
          "hid": { "gamepad_button": 1, "hold_ms": 0 },
          "state": {
            "gamelink_event": "landing_gear_changed",
            "values": {
              "deployed": { "label": "GEAR DN", "color": "#00FF88" },
              "retracted": { "label": "GEAR UP", "color": "#888888" }
            }
          }
        }
      ]
    }
  ]
}
```

| Field | Description |
|-------|-------------|
| `gamepad_button` | USB HID button index (1–32); bind this in Star Citizen |
| `hold_ms` | Hold duration before the button fires; `0` = instant tap |
| `gamelink_event` | Optional — game event name from the PC bridge that updates button state |

---

## Themes

Three visual themes are available:

| Theme | Description | Activate |
|-------|-------------|---------|
| **Default** | Dark charcoal / blue accent | Factory default |
| **Drake Military** | Amber on black, rugged military aesthetic | Theme selector screen |
| **Origin Luxury** | Ice blue on midnight, clean luxury finish | Theme selector screen |

Themes are applied globally and persisted to NVS. Panel art assets must be present on the SD card under `assets/themes/<theme>/`.

---

## PC Bridge (sc-bridge)

The optional **sc-bridge** runs on your gaming PC and connects to the terminal over WebSocket (`ws://<device-ip>/terminal` or discovered via mDNS as `sc-bridge.local:8765`).

It monitors `Game.log` for ship events and pushes state updates to the terminal so buttons can show live status (e.g., landing gear position, shield strength, power preset).

> The bridge code lives in a separate repository. The terminal firmware works standalone (USB gamepad only) without it.

---

## Web Portal

Browse to `http://<device-ip>/` (or `http://hotbox.local/`) to access the configuration portal. Available settings:

- **Ship Profile** — select active ship JSON
- **Console** — set which MFD page loads on boot
- **Wi-Fi** — SSID and password (stored in NVS, never in firmware)
- **Bridge** — hostname and port for the PC bridge WebSocket
- **Touch Calibration** — interactive calibration wizard
- **Display** — rotation (0°/90°/180°/270°)
- **Theme** — select visual theme
- **OTA Update** — upload new firmware `.bin` directly from the browser

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for coding standards, commit conventions, and the component architecture.

The short version:
- All hardware access goes through `sc_bsp` — never call LCD/touch drivers directly from `sc_ui`
- Keep assets on the SD card, not compiled into flash
- Use `MALLOC_CAP_SPIRAM` for large buffers; keep LVGL draw buffers in internal SRAM
- Prefix commits: `sc_ui: fix button grid overflow`

---

## License

This project is personal/open hardware. See `LICENSE` if present, or contact the author.

*This software is not affiliated with Cloud Imperium Games or Roberts Space Industries.*
