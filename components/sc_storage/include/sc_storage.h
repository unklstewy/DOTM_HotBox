#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the storage subsystem (SD card / VFS).
 *        Mounts the SD card to `/sdcard`.
 */
esp_err_t sc_storage_init(void);

#ifdef __cplusplus
}
#endif
