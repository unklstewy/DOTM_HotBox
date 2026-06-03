/*
 * sc_ui_screen_console.h — Ship console MFD screen
 *
 * The main screen for a terminal. Renders the active console's action grid
 * from the ship JSON config and updates button states from gamelink events.
 */
#pragma once

#include "lvgl.h"
#include "sc_config.h"
#include "sc_gamelink.h"

#ifdef __cplusplus
extern "C"
{
#endif

    lv_obj_t *sc_ui_screen_console_create(lv_obj_t *parent);
    void sc_ui_screen_console_load(const sc_terminal_config_t *cfg);
    void sc_ui_screen_console_destroy(void);
    void sc_ui_screen_console_on_event(const sc_gamelink_event_t *evt,
                                       void *user_ctx);

#ifdef __cplusplus
}
#endif
