#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize all board support hardware (IO expanders, power rails, ADC, etc).
 */
esp_err_t sc_bsp_init(void);

/**
 * @brief Initialize the display and return the LVGL display object.
 */
lv_display_t* sc_bsp_display_create(void);

/**
 * @brief Initialize the touch controller and return the LVGL indev object.
 *        Returns NULL if touch is unavailable or disabled.
 */
lv_indev_t* sc_bsp_touch_create(void);

/**
 * @brief Safely power off the device (e.g. drop the power-hold rail).
 */
void sc_bsp_power_off(void);

/**
 * @brief Get the current battery percentage (0-100).
 */
int sc_bsp_get_battery_pct(void);

/**
 * @brief Set display backlight brightness.
 */
esp_err_t sc_bsp_brightness_set(uint8_t percent);

#ifdef __cplusplus
}
#endif
