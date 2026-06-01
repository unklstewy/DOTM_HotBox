---
name: "New LVGL Screen"
description: "Scaffold a new LVGL 9 UI screen in sc_ui/screens/. Use when adding a new screen type such as a ship console view, settings page, pairing screen, or OTA update screen."
argument-hint: "screen name e.g. 'weapons_panel'"
agent: "agent"
tools: ["codebase", "create_file", "replace_string_in_file"]
---

# New LVGL Screen Scaffold

You are scaffolding a new UI screen named: **${input}**

Derive `<screen_name>` as lowercase_underscores from the argument.

## Files to Create

### 1. Header — `components/sc_ui/screens/sc_ui_screen_<screen_name>.h`
```c
#pragma once
#include "lvgl.h"
#include "sc_config.h"
#include "sc_gamelink.h"

lv_obj_t *sc_ui_screen_<screen_name>_create(lv_obj_t *parent);
void      sc_ui_screen_<screen_name>_load(const sc_terminal_config_t *cfg);
void      sc_ui_screen_<screen_name>_destroy(void);
void      sc_ui_screen_<screen_name>_on_event(sc_gamelink_event_t *evt);
```

### 2. Source — `components/sc_ui/screens/sc_ui_screen_<screen_name>.c`
Use this template:
```c
#include "sc_ui_screen_<screen_name>.h"
#include "sc_ui_theme.h"
#include "sc_hid.h"
#include "esp_log.h"

static const char *TAG = "sc_ui_<screen_name>";
static lv_obj_t *s_root = NULL;

lv_obj_t *sc_ui_screen_<screen_name>_create(lv_obj_t *parent) {
    lv_lock();
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x111111), 0);
    // TODO: populate widgets from ship config actions
    lv_unlock();
    return s_root;
}

void sc_ui_screen_<screen_name>_load(const sc_terminal_config_t *cfg) {
    ESP_LOGI(TAG, "Loading screen for console: %s", cfg->console_id);
    // TODO: bind action buttons from cfg
}

void sc_ui_screen_<screen_name>_destroy(void) {
    lv_lock();
    if (s_root) { lv_obj_delete(s_root); s_root = NULL; }
    lv_unlock();
}

void sc_ui_screen_<screen_name>_on_event(sc_gamelink_event_t *evt) {
    if (!s_root || !evt) return;
    // TODO: update widget state from game event
}
```

### 3. Register in screen router
Open `components/sc_ui/include/sc_ui_screens.h` and add the new enum value:
```c
SC_UI_SCREEN_<SCREEN_NAME>,
```

Open `components/sc_ui/sc_ui_router.c` and add the case in `sc_ui_router_push()`:
```c
case SC_UI_SCREEN_<SCREEN_NAME>:
    screen = sc_ui_screen_<screen_name>_create(lv_scr_act());
    sc_ui_screen_<screen_name>_load(cfg);
    break;
```

## Key Rules (from [.github/instructions/lvgl-ui.instructions.md](.github/instructions/lvgl-ui.instructions.md))
- ALL lv_* calls inside lv_lock() / lv_unlock()
- Frame buffers in PSRAM only
- Use SC_COL_* constants from sc_ui_theme.h for colours
