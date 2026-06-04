# WYSIWYG Layout Editor Specification & Integration Guide

This document defines the layout specifications, JSON data schemas, API protocols, and coordinate mapping systems used by the Web Portal's WYSIWYG layout builder. It serves as a developer reference for the upcoming refactor of the on-device `sc_ui` layout settings screen.

---

## 1. Coordinate Grid System

The reTerminal display panel operates as an LVGL grid layout (`LV_LAYOUT_GRID`) mapping 1:1 with the grid coordinates defined in the configuration files.

### Grid Dimensions
The dashboard layout grid dimensions are configurable on a per-console basis:
- **Default Grid**: 4 columns × 5 rows (fits D1001 LCD comfortably with 80px row heights).
- **Expanded Grids**: `grid_6x6`, `grid_8x5`, `grid_8x8`, `grid_10x5`, `grid_12x6`.

### Widget Coordinate Mappings
Every control element (momentary button, rotary knob, throttle, etc.) is placed using zero-based grid positions and row/column spans:
- `row` (integer): Target row index on the grid (starts at `0`).
- `col` (integer): Target column index on the grid (starts at `0`).
- `width` (integer): Number of columns the widget spans (default: `1`).
- `height` (integer): Number of rows the widget spans (default: `1`).

*Note: For the firmware rendering engine (`sc_ui`), column and row spans map directly to LVGL cell coordinates, ensuring the exact visual structure is replicated on-screen.*

---

## 2. JSON Configuration Schemas

### A. Ship Configuration (`/sdcard/ships/[ship_id].json`)
Represents the complete set of consoles, positions, layouts, and input action maps for a given ship.

```json
{
  "ship_id": "misc_prospector",
  "ship_name": "MISC Prospector",
  "manufacturer": "Musashi Industrial & Starflight Concern",
  "consoles": [
    {
      "console_id": "mining_operations",
      "display_name": "Mining Operations Console",
      "position": "pilot",
      "layout": "grid_6x6",
      "actions": [
        {
          "id": "mining_laser_power",
          "label": "LASER PWR",
          "description": "Toggle Mining Laser Power",
          "icon": "btn_latching",
          "row": 0,
          "col": 0,
          "width": 1,
          "height": 1,
          "widget_type": "btn_latching",
          "hid": {
            "consumer_usage": 0,
            "hold_ms": 0,
            "gamepad_button": 1
          },
          "state": {
            "gamelink_event": "mining_laser_active",
            "values": {
              "active": { "label": "LASER ON", "color": "#FF8800" },
              "idle": { "label": "LASER OFF", "color": "#00FF88" }
            }
          }
        }
      ]
    }
  ]
}
```

### B. Ship Templates Database (`/sdcard/ships/ship_templates.json`)
Maintains the internal database of pre-configured ship models, consoles, and specialized control elements (e.g. mining consoles, medical bays) that users can instantiate.

```json
{
  "templates": [
    {
      "ship_id": "misc_prospector",
      "ship_name": "MISC Prospector",
      "manufacturer": "Musashi Industrial & Starflight Concern",
      "consoles": [ ... ]
    },
    {
      "ship_id": "rsi_scorpius",
      "ship_name": "RSI Scorpius",
      "manufacturer": "Roberts Space Industries",
      "consoles": [ ... ]
    }
  ]
}
```

---

## 3. Backend HTTP REST API

The ESP32-P4 firmware (`components/sc_web`) exposes REST endpoints to allow the web editor to read/write ship config files and control the running device:

### 1. `GET /api/fs/list?path=[directory_path]`
Lists the contents of the given path on the SD card (`/sdcard`). Used to dynamically discover ship layouts.
* **Response format**:
  ```json
  [
    { "name": "cutlass_black.json", "is_dir": false, "size": 20566 },
    { "name": "ship_templates.json", "is_dir": false, "size": 8412 }
  ]
  ```

### 2. `GET /api/fs/read?path=[file_path]`
Streams the raw JSON content of the requested layout or template file.
* **Query Parameters**: `path=/ships/misc_prospector.json`

### 3. `POST /api/fs/upload`
Saves the serialized JSON file to the SD card.
* **Headers**: `X-File-Path: /sdcard/ships/misc_prospector.json`, `Content-Type: application/octet-stream`
* **Body**: Raw JSON text payload.

### 4. `POST /api/config`
Updates the active target console running on the physical hardware screen.
* **Body**:
  ```json
  {
    "ship_id": "misc_prospector",
    "console_id": "mining_operations",
    "terminal_index": 0
  }
  ```

---

## 4. `sc_ui` On-Device Refactor Integration Guide

During the upcoming `sc_ui` layout settings screen refactor, the device can replicate these drag-and-drop feature sets by utilizing the exposed endpoints and coordinates:

1. **Reading Ship List & Templates**:
   The firmware can parse `/sdcard/ships/ship_templates.json` using the local C JSON parser (e.g. `cJSON`) to let users create a new layout directly from the touch screen using preset profiles.
2. **On-Device Grid Repositioning**:
   - The on-device settings screen can render an interactive layout grid representing the active console's columns and rows.
   - Using LVGL touch gestures (`LV_EVENT_DRAG_BEGIN`, `LV_EVENT_DRAG_END`), users can drag widget objects across grid boundaries.
   - When a widget is dropped, the firmware can calculate the nearest grid cell:
     ```c
     int target_col = touch_x / cell_width;
     int target_row = touch_y / cell_height;
     ```
   - Update the widget's action `row` and `col` properties, write the updated JSON structure back to `/sdcard/ships/[ship_id].json`, and call `sc_ui_reload()` to refresh the dashboard instantly.
