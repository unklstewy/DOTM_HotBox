# Contributing Guidelines

Welcome! This project is being refactored to support maximum flexibility across various ESP32 boards (specifically ESP32-P4 and ESP32-C6) and display controllers. 

To achieve this, we are treating all ESP-IDF components (`components/*`) as **independent, reusable public libraries**. 

## Architectural Mindset

When contributing to this project, adhere to the following principles:

1. **Hardware Independence**: 
   No component (especially `sc_ui` or `sc_gamelink`) should know about specific hardware drivers (e.g., `esp_lcd_jd9365_8` or `esp_lcd_touch_gsl3670`). Hardware must be abstracted behind the `sc_bsp` (Board Support Package) layer. If you need to turn on the screen, call `sc_bsp_display_enable()`, do not talk to the IO expander directly.

2. **Loose Coupling**:
   Components should interact via callbacks, event loops, or opaque handles. Do not tightly couple `sc_ui` to `sc_hid`. Use `sc_gamelink` or the system event loop to broadcast generic state changes.

3. **Memory Consciousness**:
   - Prefer loading assets (images, fonts, JSON) from the SD Card via the VFS rather than baking them into Flash.
   - For headless/HotBox Lite targets (e.g., ESP32-S3/C3 without an SD card slot), assets are loaded from the **SPIFFS partition image**. Keep SPIFFS asset sizes minimal (e.g., only copy lightweight layout SVGs, not huge pre-rasterized `.bin` directories) to avoid exceeding internal flash storage constraints.
   - Use PSRAM (MALLOC_CAP_SPIRAM) for large buffers, but keep tight, high-frequency LVGL draw buffers in internal SRAM to prevent memory bandwidth starvation.

4. **Comments & Documentation**:
   - Write clear Doxygen-style comments for public header API functions.
   - Separate concerns cleanly into distinct `.c` files.
   - If you introduce a complex state machine, document its flow at the top of the C file.

5. **Headless & Hybrid Operations (HotBox Lite)**:
   - Ensure frontend-backend communication remains clean. The web portal's **Play Mode** renders the control canvas in the browser and sends user events to the device. Any new widgets, interaction models (e.g., pointer captures for smooth touch dragging), or layout features must work/fail gracefully on both the native LVGL display and the web-based canvas interface.

## Commit Standards

- Make atomic commits that address a single concern.
- Prefix commits with the component name (e.g., `sc_ui: Refactor button grid layout`).
- Ensure the code builds locally (`idf.py build`) before submitting. GitHub CI is not currently validating builds.
