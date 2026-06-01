---
description: "Use when writing or reviewing ESP-IDF C firmware components under components/**. Covers naming, error handling, logging, FreeRTOS tasks, PSRAM allocation, NVS access, and SPIFFS usage patterns for this project."
applyTo: "components/**/*.{c,h}"
---

# ESP-IDF Component Conventions

## Module Structure
Every component must expose exactly these four symbols in its public header:
```c
esp_err_t sc_<name>_init(void);          // called once from main, sets up task
void      sc_<name>_deinit(void);        // clean teardown (OTA, low-power)
```

## Naming
- Functions: `sc_<component>_<noun>_<verb>()` → `sc_hid_action_send()`
- Types: `sc_<component>_<noun>_t` → `sc_config_ship_t`
- Macros/constants: `SC_<COMPONENT>_<NAME>` → `SC_HID_MAX_ACTIONS`
- TAG: `static const char *TAG = "sc_<component>";`

## Error Handling
```c
// Always return esp_err_t from init/deinit:
esp_err_t sc_foo_init(void) {
    esp_err_t ret = do_something();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "do_something failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}
// At call sites in main use:
ESP_ERROR_CHECK(sc_foo_init());
```

## Logging
```c
static const char *TAG = "sc_foo";
ESP_LOGI(TAG, "Started with param=%d", val);   // INFO for normal ops
ESP_LOGW(TAG, "Unexpected state: %d", state);  // WARN for recoverable
ESP_LOGE(TAG, "Fatal: %s", esp_err_to_name(ret)); // ERROR for failures
ESP_LOGD(TAG, "Debug value: %p", ptr);          // DEBUG (stripped in release)
```

## FreeRTOS Tasks
```c
// Priority bands: UI=5, HID=10, Network=7, GameLink=6
// Stack MUST be allocated from PSRAM:
static StaticTask_t s_task_buf;
static StackType_t  s_task_stack[SC_FOO_STACK_SIZE]; // define in PSRAM section

xTaskCreateStatic(
    sc_foo_task,
    "sc_foo",
    SC_FOO_STACK_SIZE,
    NULL,
    SC_FOO_TASK_PRIORITY,
    s_task_stack,
    &s_task_buf
);
```

## NVS Access
- Namespace: `"sc_<component>"` (max 15 chars)
- Always call `nvs_commit()` after writes
- Never store raw Wi-Fi passwords in NVS keys with obvious names; use `"wifi_psk"`

## SPIFFS (ship data)
- Mount point: `/spiffs`
- Ship JSON path: `/spiffs/ships/<ship_id>.json`
- Open read-only: `fopen(path, "r")` — never write to SPIFFS at runtime
