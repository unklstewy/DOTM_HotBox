if(IDF_TARGET STREQUAL "esp32s3")
    set(extra_reqs spiffs)
else()
    set(extra_reqs vfs fatfs sdmmc esp_lcd)
endif()

idf_component_register(
    SRCS "sc_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES ${extra_reqs}
)
