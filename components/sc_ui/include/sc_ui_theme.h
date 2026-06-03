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

typedef struct {
    lv_color_t armed;
    lv_color_t ready;
    lv_color_t warn;
    lv_color_t off;
    lv_color_t accent;
    lv_color_t bg;
    lv_color_t bg_panel;
    lv_color_t text;
    lv_color_t text_dim;
    lv_color_t divider;
} sc_theme_colors_t;

extern sc_theme_colors_t sc_theme;

/* ── Brand Palette ───────────────────────────────────────────────────────── */
#define SC_COL_ARMED    (sc_theme.armed)   /**< Weapon armed / danger   */
#define SC_COL_READY    (sc_theme.ready)   /**< System online / safe    */
#define SC_COL_WARN     (sc_theme.warn)    /**< Caution / degraded      */
#define SC_COL_OFF      (sc_theme.off)     /**< System offline          */
#define SC_COL_ACCENT   (sc_theme.accent)  /**< UI highlight / selected */
#define SC_COL_BG       (sc_theme.bg)      /**< Screen background       */
#define SC_COL_BG_PANEL (sc_theme.bg_panel)/**< Panel / card background */
#define SC_COL_TEXT     (sc_theme.text)    /**< Primary text            */
#define SC_COL_TEXT_DIM (sc_theme.text_dim)/**< Secondary / disabled    */
#define SC_COL_DIVIDER  (sc_theme.divider) /**< Separator lines         */

/* ── Theme API ───────────────────────────────────────────────────────────── */
void sc_ui_theme_init_default(void);
void sc_ui_theme_init_drake_military(void);
void sc_ui_theme_init_origin_lux(void);
void sc_ui_theme_set(const sc_theme_colors_t *colors);

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
