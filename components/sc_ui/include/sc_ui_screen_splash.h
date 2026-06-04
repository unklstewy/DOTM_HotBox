#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the splash screen displaying the custom portrait background,
 *        branding logo, and a progress bar representing SVG rasterization status.
 * @param user_data Optional user data pointer.
 * @return lv_obj_t* The created screen object.
 */
lv_obj_t *sc_ui_screen_splash_create(void *user_data);

#ifdef __cplusplus
}
#endif
