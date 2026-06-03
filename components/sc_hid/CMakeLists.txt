idf_component_register(
    SRCS
        "sc_hid.c"
        "sc_hid_desc.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        espressif__esp_tinyusb
        espressif__cjson
        esp_timer
)

# TinyUSB callback functions (tud_hid_get_report_cb etc.) are referenced FROM
# the TinyUSB library, not FROM our application code.  In a flat link without
# --start-group, sc_hid_desc.c.obj would be skipped when the archive is first
# scanned (no unresolved refs to it yet) and then TinyUSB would fail with
# "undefined reference".  Linking sc_hid with --whole-archive ensures the
# callbacks are always present in the final image regardless of link order.
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
