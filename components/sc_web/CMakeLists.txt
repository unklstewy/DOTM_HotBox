set(extra_reqs)
if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND extra_reqs sc_ui)
endif()

idf_component_register(SRCS "sc_web.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server nvs_flash sc_config sc_network driver esp_driver_ledc sc_hid ${extra_reqs})
