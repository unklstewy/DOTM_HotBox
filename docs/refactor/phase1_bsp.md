# Agent Instruction: Phase 1 - Hardware Abstraction Layer (BSP)

**Objective**: Decouple hardware-specific initialization and control from `sc_ui.c` and `main.c` by creating a new Board Support Package (`sc_bsp`) component.

## Context
Currently, the UI component (`sc_ui.c`) directly initializes the PCA9535 IO expander, the JD9365 MIPI-DSI display driver, and the GSL3670 touch controller. It also directly reads `GPIO3` for the power button and `GPIO15` for the battery ADC. This tightly pins the project to the SeeedStudio reTerminal D1001 hardware. We need this project to support arbitrary ESP32-P4 and ESP32-C6 targets.

## Execution Steps

1. **Create the `sc_bsp` component**
   - Create `components/sc_bsp/CMakeLists.txt`.
   - Create `components/sc_bsp/include/sc_bsp.h`. This header should define generic APIs:
     - `esp_err_t sc_bsp_init(void);`
     - `lv_display_t* sc_bsp_display_create(void);`
     - `lv_indev_t* sc_bsp_touch_create(void);`
     - `void sc_bsp_power_off(void);`
     - `int sc_bsp_get_battery_pct(void);`
   
2. **Move Hardware Logic**
   - Move all PCA9535, JD9365, and GSL3670 logic out of `sc_ui.c` into `components/sc_bsp/sc_bsp_d1001.c`.
   - Move the ADC oneshot battery reading and power button interrupt/polling logic into the BSP.
   - Update `CMakeLists.txt` to conditionally compile `sc_bsp_d1001.c` based on a Kconfig or just default to it for now.
   - Move all hardware-specific `REQUIRES` (e.g., `esp_lcd_jd9365_8`, `esp_io_expander_pca9535`) from `sc_ui/CMakeLists.txt` to `sc_bsp/CMakeLists.txt`.

3. **Update `sc_ui`**
   - Refactor `sc_ui_init()` to call `sc_bsp_init()`.
   - Refactor the LVGL buffer setup to rely on the BSP to return a configured `lv_display_t*` and `lv_indev_t*`. 
   - `sc_ui.c` should no longer include any hardware driver headers (like `esp_lcd_mipi_dsi.h` or `esp_lcd_touch_gsl3670.h`).

4. **Verify**
   - Ensure `idf.py build` compiles successfully.
   - The UI should function exactly as before, but the dependency graph is now cleanly separated.
