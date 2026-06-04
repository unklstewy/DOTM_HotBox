idf_component_register(
    SRCS "sc_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES vfs fatfs sdmmc esp_lcd
)
