---
name: "Flash and Monitor"
description: "Build the SC Terminal firmware, flash it to the connected reTerminal D1001, and open the serial monitor. Use when you want to build, deploy, and debug the firmware on hardware."
agent: "agent"
tools: ["run_in_terminal"]
---

# Flash and Monitor — SC Terminal

Build, flash, and open the IDF monitor for the reTerminal D1001 (ESP32-P4).

## Pre-flight Checks
1. Confirm `IDF_PATH` is set: run `echo $env:IDF_PATH` (Windows PowerShell) or `echo $IDF_PATH` (Linux/macOS)
2. Confirm target is `esp32p4`: run `idf.py get-target` — if wrong, run `idf.py set-target esp32p4`
3. Confirm the device port. On Windows use Device Manager; on Linux `ls /dev/ttyACM*` or `ls /dev/ttyUSB*`

## Build
```bash
cd /home/sannis/ESPSCar
idf.py build
```
Fix any errors before proceeding.

## Flash
```powershell
# Replace COMx with the actual port (e.g. COM5 on Windows, /dev/ttyACM0 on Linux)
idf.py -p COMx flash
```

Hold the BOOT button on the reTerminal during the "Connecting..." phase if needed.

## Monitor
```powershell
idf.py -p COMx monitor
```
Press `Ctrl+]` to exit the monitor.

## Combined (build + flash + monitor in one command)
```powershell
idf.py -p COMx flash monitor
```

## Common Issues
| Symptom | Fix |
|---|---|
| `A fatal error occurred: Failed to connect` | Hold BOOT button during Connecting phase |
| `Component not found` | Run `idf.py update-dependencies` first |
| Garbled output | Confirm monitor baud is 115200 (default for IDF) |
| LVGL panics on boot | Check PSRAM is enabled: `CONFIG_SPIRAM=y` in sdkconfig |
| USB HID not enumerated | Check `CONFIG_TINYUSB_ENABLED=y` and USB cable supports data |

## Partition Erase (full reflash)
If NVS is corrupt or you changed the partition table:
```powershell
idf.py -p COMx erase-flash
idf.py -p COMx flash
```
