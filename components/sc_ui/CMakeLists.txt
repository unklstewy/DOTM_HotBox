idf_component_register(
    SRCS
        "sc_ui.c"
        "sc_ui_theme.c"
        "screens/sc_ui_screen_console.c"
        "screens/sc_ui_screen_settings.c"
        "screens/sc_ui_screen_pairing.c"
        "screens/sc_ui_screen_calibration.c"
        "screens/sc_ui_screen_drake.c"
        "screens/sc_ui_screen_origin.c"
        "screens/sc_ui_screen_theme_selector.c"
    INCLUDE_DIRS
        "include"
        "screens"
    REQUIRES
        lvgl__lvgl
        esp_lcd_jd9365_8
        esp_lcd_touch_gsl3670
        esp_io_expander_pca9535
        esp_lcd
        esp_driver_ledc
        driver
        sc_config
        sc_hid
        sc_gamelink
        esp_timer
        nvs_flash
)

target_add_binary_data(${COMPONENT_LIB} "assets/mockup_drake.bin" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/mockup_origin.bin" BINARY)
