if(NOT IDF_TARGET STREQUAL "esp32p4")
    return()
endif()

idf_component_register(SRCS "esp_io_expander_pca9535.c"
                    INCLUDE_DIRS "include" REQUIRES "driver" "esp_driver_i2c")

