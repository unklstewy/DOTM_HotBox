# Agent Instruction: Phase 3 - SD Card & VFS Asset Management

**Objective**: Free up Flash memory and PSRAM by initializing an SD Card, mounting it to the ESP-IDF Virtual File System (VFS), and enabling LVGL to load UI assets directly from it.

## Context
Currently, the UI project embeds UI sprites, images, and fonts as compiled C-arrays into the firmware flash, taking up valuable code space and RAM. The SeeedStudio reTerminal D1001 (and upcoming custom boards) feature an SD Card slot. By moving assets to the SD Card, we make the firmware smaller, boot faster, and allow dynamic theme changes without firmware updates. We also want to support OTA bundles stored on the SD Card.

## Execution Steps

1. **Create the `sc_storage` Component**
   - Create `components/sc_storage/CMakeLists.txt` and `components/sc_storage/include/sc_storage.h`.
   - Implement `sc_storage_init()` which handles either SDMMC or SPI initialization based on `sc_bsp` configurations.
   - Mount the SD card using `esp_vfs_fat_sdspi_mount` or `esp_vfs_fat_sdmmc_mount` to the `/sdcard` path.

2. **Integrate LVGL File System (`lv_fs_if`)**
   - Enable `LV_USE_FS_STDIO` or `LV_USE_FS_POSIX` in `lv_conf.h` (or via Kconfig menuconfig) and configure it with the drive letter `"S:"` mapped to `/sdcard/`.
   - Update `sc_ui.c` to call `lv_fs_stdio_init()` or initialize the POSIX FS interface after LVGL initialization.

3. **Refactor Asset Loading**
   - Modify the python scripts in `tools/mock_ui/` to export UI mockups as raw binary files (`.bin` or `.png`) instead of generating `sc_ui_theme_sprites.h` with C-arrays.
   - Refactor `sc_ui_theme.c` and screen logic to set image sources using LVGL file paths (e.g., `lv_image_set_src(img, "S:/assets/themes/drake/btn_idle.bin")`).
   - Remove the old `assets/sprites/*.c` files from `components/sc_ui/CMakeLists.txt`.

4. **Verify**
   - Compile locally with `idf.py build`.
   - Verify there are no missing symbols regarding the old image C-arrays.
