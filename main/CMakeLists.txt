set(extra_reqs)
if(CONFIG_IDF_TARGET_ESP32P4)
    list(APPEND extra_reqs sc_ui)
endif()

idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash
        esp_partition
        sc_hid
        sc_network
        sc_config
        sc_gamelink
        sc_storage
        sc_web
        ${extra_reqs}
)
