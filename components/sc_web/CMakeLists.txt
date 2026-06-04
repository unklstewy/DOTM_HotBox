idf_component_register(SRCS "sc_web.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server nvs_flash sc_config sc_network driver esp_driver_ledc
                       EMBED_FILES "../../web_portal/dist/index.html"
                                   "../../web_portal/dist/assets/index.js"
                                   "../../web_portal/dist/assets/index.css")
