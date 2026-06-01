cmake_minimum_required(VERSION 3.16)

# ──────────────────────────────────────────────────────────────────────────────
#  Star Citizen Terminal — Top-level CMake
#  Hardware : Seeed Studio reTerminal D1001 (ESP32-P4NRW32 + ESP32-C6)
#  Firmware : ESP-IDF ≥ 5.3
# ──────────────────────────────────────────────────────────────────────────────

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sc_terminal VERSION 1.0.0)

# Build a SPIFFS image from ./data and flash it to the "storage" partition.
# This provides /spiffs/ships/<ship_id>.json at runtime.
spiffs_create_partition_image(storage ${CMAKE_CURRENT_LIST_DIR}/data FLASH_IN_PROJECT)

# ── Linker workarounds for IDF 5.5 + esp_hosted 0.0.10 ──────────────────────
# esp_hosted 0.0.10 defines wifi_deinit/wifi_set_country/wifi_get_country to
# forward calls to the ESP32-C6 coprocessor.  IDF 5.5's updated libnet80211.a
# (esp32_host variant) also defines these same symbols.  --allow-multiple-definition
# causes ld to silently keep the FIRST definition seen; esp_hosted is linked with
# --whole-archive so its versions always appear first and win — which is correct.
target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE
    -Wl,--allow-multiple-definition
)
