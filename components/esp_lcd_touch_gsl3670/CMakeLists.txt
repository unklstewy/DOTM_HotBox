if(NOT IDF_TARGET STREQUAL "esp32p4")
    return()
endif()

idf_component_register(SRCS "esp_lcd_touch_gsl3670.c" "gsl_point_id.c" INCLUDE_DIRS "include" REQUIRES "esp_lcd")

