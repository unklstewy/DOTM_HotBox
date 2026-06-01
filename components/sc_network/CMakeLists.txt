idf_component_register(
    SRCS
        "sc_network.c"
        "wifi_hal_stubs.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        sc_config
        esp_wifi
        esp_http_client
        espressif__esp_websocket_client
        espressif__mdns
        esp_event
        esp_netif
        nvs_flash
)

# wifi_hal_stubs.c defines NAN HAL symbols referenced by libnet80211.a
# (ieee80211_nan_common.o).  Those are pulled in late by the linker, after
# sc_network has already been scanned.  --whole-archive forces wifi_hal_stubs
# into the image regardless of scan order.
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
