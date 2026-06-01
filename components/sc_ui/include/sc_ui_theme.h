/*
 * sc_ui_theme.h — Colour palette and style constants for SC Terminal UI
 *
 * Import this in every screen/widget file that needs colours or fonts.
 * All values use LVGL 9 lv_color_hex() macro.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Brand Palette ───────────────────────────────────────────────────────── */
#define SC_COL_ARMED    lv_color_hex(0xFF4444)   /**< Weapon armed / danger   */
#define SC_COL_READY    lv_color_hex(0x00FF88)   /**< System online / safe    */
#define SC_COL_WARN     lv_color_hex(0xFFAA00)   /**< Caution / degraded      */
#define SC_COL_OFF      lv_color_hex(0x333333)   /**< System offline          */
#define SC_COL_ACCENT   lv_color_hex(0x0099FF)   /**< UI highlight / selected */
#define SC_COL_BG       lv_color_hex(0x111111)   /**< Screen background       */
#define SC_COL_BG_PANEL lv_color_hex(0x1A1A2E)   /**< Panel / card background */
#define SC_COL_TEXT     lv_color_hex(0xE0E0E0)   /**< Primary text            */
#define SC_COL_TEXT_DIM lv_color_hex(0x888888)   /**< Secondary / disabled    */
#define SC_COL_DIVIDER  lv_color_hex(0x2A2A3E)   /**< Separator lines         */

/* ── Radius / Padding ────────────────────────────────────────────────────── */
#define SC_UI_BTN_RADIUS    (8)
#define SC_UI_PANEL_RADIUS  (12)
#define SC_UI_PAD_SMALL     (8)
#define SC_UI_PAD_MEDIUM    (16)
#define SC_UI_PAD_LARGE     (24)

/* ── Fonts (LVGL built-in fallbacks; replace with custom Montserrat build) ─ */
#define SC_FONT_SMALL   (&lv_font_montserrat_14)
#define SC_FONT_MEDIUM  (&lv_font_montserrat_20)
#define SC_FONT_LARGE   (&lv_font_montserrat_28)
#define SC_FONT_TITLE   (&lv_font_montserrat_36)

#ifdef __cplusplus
}
#endif
