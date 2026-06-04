/*
 * sc_web.h — Web Management Portal HTTP Server
 *
 * Hosts the remote control configuration UI and file manager.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the HTTP web server and register all portal and API handlers.
 * @return ESP_OK on success, or appropriate error code.
 */
esp_err_t sc_web_start(void);

/**
 * @brief Stop the HTTP web server.
 */
void sc_web_stop(void);

#ifdef __cplusplus
}
#endif
