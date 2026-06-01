---
name: "New FreeRTOS Task"
description: "Scaffold a new FreeRTOS task in any ESP-IDF component. Use when adding background processing, a new subsystem loop, or a periodic timer task inside a firmware component."
argument-hint: "task purpose e.g. 'websocket receive loop in sc_network'"
agent: "agent"
tools: ["codebase", "replace_string_in_file"]
---

# New FreeRTOS Task Scaffold

You are adding a new FreeRTOS task for: **${input}**

Determine from context:
- `<component>` — which component owns this task (`sc_hid`, `sc_network`, etc.)
- `<task_name>` — short descriptive name in snake_case
- `priority` — from priority band: UI=5, HID=10, Network=7, GameLink=6

## 1. Add Task Constants to the component header
In `components/<component>/include/<component>.h`:
```c
#define SC_<COMPONENT>_<TASK_NAME>_STACK_SIZE   (4096)
#define SC_<COMPONENT>_<TASK_NAME>_TASK_PRIORITY (7)   // adjust to band
```

## 2. Declare Static Buffers in the .c file
```c
// Static allocation — avoids heap fragmentation at runtime
static StaticTask_t s_<task_name>_buf;
// Stack in PSRAM — mandatory for all tasks in this project
static StackType_t  s_<task_name>_stack[SC_<COMPONENT>_<TASK_NAME>_STACK_SIZE]
    __attribute__((aligned(4)));
static TaskHandle_t s_<task_name>_handle = NULL;
```

## 3. Implement the Task Function
```c
static void sc_<component>_<task_name>_task(void *arg)
{
    static const char *TAG = "sc_<component>_<task_name>";
    ESP_LOGI(TAG, "Task started");

    for (;;) {
        // TODO: implement task body
        // Use ulTaskNotifyTake() / xQueueReceive() instead of vTaskDelay
        // for event-driven tasks. Only use vTaskDelay for periodic tasks.
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Tasks must never return — if reached, delete self:
    vTaskDelete(NULL);
}
```

## 4. Start the Task from _init()
Inside `sc_<component>_init()`:
```c
s_<task_name>_handle = xTaskCreateStatic(
    sc_<component>_<task_name>_task,
    "sc_<component>_<task_name>",
    SC_<COMPONENT>_<TASK_NAME>_STACK_SIZE,
    NULL,
    SC_<COMPONENT>_<TASK_NAME>_TASK_PRIORITY,
    s_<task_name>_stack,
    &s_<task_name>_buf
);
if (!s_<task_name>_handle) {
    ESP_LOGE(TAG, "Failed to create <task_name> task");
    return ESP_FAIL;
}
```

## Key Constraints (from [.github/instructions/esp-idf-component.instructions.md](.github/instructions/esp-idf-component.instructions.md))
- Stack MUST use `StackType_t` array — never `pvPortMalloc` for task stacks
- PSRAM-backed stacks: add `__attribute__((section(".ext_ram.bss")))` if linker script requires
- Priority band must match: UI=5, HID=10, Network=7, GameLink=6
- Tasks should be event-driven (queues/notifications) not polling with vTaskDelay
