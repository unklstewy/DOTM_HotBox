/*
 * sc_ui_screens.h — Screen ID enum and common screen interface
 *
 * Include this in sc_ui.c and in any code that calls sc_ui_router_push().
 */
#pragma once

#include "sc_ui.h"   /* for sc_ui_screen_id_t */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Each screen module implements these four functions.
 * They are forward-declared here so sc_ui_router can call them without
 * including every screen header directly.
 */

/* sc_ui_screen_console */
#include "sc_ui_screen_console.h"

/* sc_ui_screen_settings */
#include "sc_ui_screen_settings.h"

/* sc_ui_screen_pairing */
#include "sc_ui_screen_pairing.h"

#include "sc_ui_screen_drake.h"
#include "sc_ui_screen_origin.h"
#include "sc_ui_screen_theme_selector.h"

#ifdef __cplusplus
}
#endif
