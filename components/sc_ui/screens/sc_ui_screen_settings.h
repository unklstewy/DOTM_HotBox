/*
 * sc_ui_screen_settings.h — Terminal settings screen
 */
#pragma once
#include "lvgl.h"
#include "sc_config.h"
#include "sc_gamelink.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *sc_ui_screen_settings_create(lv_obj_t *parent);
void      sc_ui_screen_settings_load(const sc_terminal_config_t *cfg);
void      sc_ui_screen_settings_destroy(void);
void      sc_ui_screen_settings_on_event(const sc_gamelink_event_t *evt);

#ifdef __cplusplus
}
#endif
