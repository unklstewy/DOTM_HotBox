/*
 * sc_ui_theme.h — Colour palette, style constants, and widget draw helpers
 *
 * Import this in every screen/widget file that needs colours, fonts, or the
 * styled widget factory functions.
 *
 * All colours use LVGL 9 lv_color_hex().
 * All widget draw functions return the interactive/content child object so
 * callers can attach event handlers and set values.
 *
 * Widget rendering strategy
 * ─────────────────────────
 * After the sprite atlas refactor, every visible surface comes from the PSRAM-
 * resident atlas (loaded by sc_ui_sprites_load in theme init).  Functions here
 * retrieve lv_image_dsc_t pointers from sc_ui_sprites_get() and apply them as
 * bg-image sources — zero SD-card reads at draw time.
 */
#pragma once

#include "lvgl.h"
#include "sc_ui_sprites.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Theme colour palette ────────────────────────────────────────────────── */

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

typedef enum {
    SC_THEME_DEFAULT = 0,
    SC_THEME_DRAKE_MILITARY,
    SC_THEME_ORIGIN_LUX
} sc_theme_id_t;

extern sc_theme_colors_t sc_theme;

/* ── Brand colour macros ─────────────────────────────────────────────────── */
#define SC_COL_ARMED    (sc_theme.armed)
#define SC_COL_READY    (sc_theme.ready)
#define SC_COL_WARN     (sc_theme.warn)
#define SC_COL_OFF      (sc_theme.off)
#define SC_COL_ACCENT   (sc_theme.accent)
#define SC_COL_BG       (sc_theme.bg)
#define SC_COL_BG_PANEL (sc_theme.bg_panel)
#define SC_COL_TEXT     (sc_theme.text)
#define SC_COL_TEXT_DIM (sc_theme.text_dim)
#define SC_COL_DIVIDER  (sc_theme.divider)

/* ── Widget type enum ────────────────────────────────────────────────────── */

/**
 * @brief Logical widget type used by the console screen factory.
 *
 * Parsed from the "widget_type" field in the ship JSON action entries.
 * Determines which sprite sub-set and LVGL widget tree is created.
 */
typedef enum {
    SC_WIDGET_BTN_MOMENTARY = 0,  /**< Standard momentary push-button          */
    SC_WIDGET_BTN_LATCHING,       /**< Latching toggle (ON / OFF state)        */
    SC_WIDGET_SLIDER_H,           /**< Horizontal slider                       */
    SC_WIDGET_SLIDER_V,           /**< Vertical slider                         */
    SC_WIDGET_AXIS_JOYSTICK,      /**< 2-axis joystick (XY thumb)              */
    SC_WIDGET_AXIS_DPAD,          /**< D-Pad / hat switch (4-direction)        */
    SC_WIDGET_AXIS_HAAT,          /**< HAAT — head-actuated articulated thumb  */
    SC_WIDGET_AXIS_THROTTLE,      /**< Throttle linear axis (vertical)         */
    SC_WIDGET_AXIS_YAW,           /**< Yaw rotary axis                         */
    SC_WIDGET_AXIS_RUDDER,        /**< Rudder pedal horizontal axis            */
    SC_WIDGET_KNOB,               /**< Rotary knob                             */
    SC_WIDGET_JOG_WHEEL,          /**< Jog / shuttle wheel (multi-frame)       */
    SC_WIDGET_BTN_DANGER,         /**< Eject / armed-hot danger button         */
    SC_WIDGET_COUNT
} sc_widget_type_t;

typedef struct
{
    char action_id[32];
    char label_text[32];
    lv_obj_t *widget;        /* root widget object (any type) */
    lv_obj_t *label;         /* text label child, if applicable */
    int console_idx;
    char state_event[48];    /* gamelink event name, or ""  */
    /* Layout Geometry */
    int row;
    int col;
    int width;
    int height;
    /* Widget type */
    sc_widget_type_t widget_type;
    bool latching_state;     /* current ON/OFF for latching buttons */
    /* State colours from JSON */
    char state_keys[4][16];
    lv_color_t state_colors[4];
    char state_labels[4][16];
    uint8_t state_count;
    /* Custom computed rasterized bounds */
    int pixel_w;
    int pixel_h;
} console_btn_t;

/* ── Theme API ───────────────────────────────────────────────────────────── */

/**
 * @brief Initialise default (non-themed) colour palette.
 *        Does NOT load an atlas — used on boot before theme selection.
 */
void sc_ui_theme_init_default(void);

/**
 * @brief Activate the Drake Military theme.
 *        Unloads any previously resident atlas, loads the Drake atlas from SD,
 *        and sets the colour palette.
 */
void sc_ui_theme_init_drake_military(void);

/**
 * @brief Activate the Origin Lux theme.
 *        Same lifecycle as Drake variant.
 */
void sc_ui_theme_init_origin_lux(void);

/** @brief Override the current colour palette at runtime (no atlas change). */
void sc_ui_theme_set(const sc_theme_colors_t *colors);

/** @brief Return the currently active theme ID. */
sc_theme_id_t sc_ui_theme_get_active(void);

/* ── Layout constants ────────────────────────────────────────────────────── */
#define SC_UI_BTN_RADIUS    (8)
#define SC_UI_PANEL_RADIUS  (12)
#define SC_UI_PAD_SMALL     (8)
#define SC_UI_PAD_MEDIUM    (16)
#define SC_UI_PAD_LARGE     (24)

/* Standard sprite-based widget sizes (px) — must match atlas layout. */
#define SC_UI_BTN_W         (140)
#define SC_UI_BTN_H         (56)
#define SC_UI_AXIS_W        (120)
#define SC_UI_AXIS_H        (120)
#define SC_UI_THROTTLE_W    (44)
#define SC_UI_THROTTLE_H    (120)
#define SC_UI_KNOB_W        (64)
#define SC_UI_KNOB_H        (64)
#define SC_UI_JOG_W         (96)
#define SC_UI_JOG_H         (96)

/* ── Fonts ───────────────────────────────────────────────────────────────── */
#define SC_FONT_SMALL   (&lv_font_montserrat_14)
#define SC_FONT_MEDIUM  (&lv_font_montserrat_20)
#define SC_FONT_LARGE   (&lv_font_montserrat_28)
#define SC_FONT_TITLE   (&lv_font_montserrat_36)

/* ── Panel draw helper ───────────────────────────────────────────────────── */

/**
 * @brief Decorate a container with 9-slice panel chrome from the active atlas.
 *
 * Reads SC_SPRITE_PANEL_* sprites — no SD card access.
 * Returns the inner content container (transparent, padded inside the chrome).
 *
 * @param panel  Parent lv_obj_t to decorate.
 * @return       Inner content lv_obj_t for child widgets.
 */
lv_obj_t *sc_ui_theme_draw_panel(lv_obj_t *panel);

/* ── Button style helpers ────────────────────────────────────────────────── */

/**
 * @brief Apply the sprite-backed visual style to a button.
 *
 * Selects the correct atlas sub-image based on state_color proximity to the
 * theme's armed/ready/warn/accent colours.
 *
 * @param btn          LVGL button object.
 * @param state_color  Current logical state colour (SC_COL_ARMED etc.).
 */
void sc_ui_theme_style_btn(lv_obj_t *btn, lv_color_t state_color);

/**
 * @brief Apply sprite for a latching toggle button.
 *
 * @param btn    LVGL button object.
 * @param is_on  true = latching ON state, false = OFF state.
 */
void sc_ui_theme_style_btn_latching(lv_obj_t *btn, bool is_on);

/**
 * @brief Apply tab-strip styling (active / inactive).
 */
void sc_ui_theme_style_tab(lv_obj_t *tab, bool is_active);

/* ── Widget factory helpers ──────────────────────────────────────────────── */

/**
 * @brief Create a sprite-backed joystick widget.
 *
 * Returns the base lv_obj_t.  The thumb sub-object is the first child.
 * Caller positions thumb by setting child coordinates on touch events.
 */
lv_obj_t *sc_ui_theme_draw_axis_joystick(lv_obj_t *parent, console_btn_t *cb);

/**
 * @brief Create a sprite-backed D-Pad widget.
 *
 * Returns the base container.  Call sc_ui_theme_dpad_set_dir() to highlight
 * the active direction.
 */
lv_obj_t *sc_ui_theme_draw_axis_dpad(lv_obj_t *parent, console_btn_t *cb);

/**
 * @brief Highlight a D-Pad direction (pass LV_DIR_NONE to clear all).
 */
void sc_ui_theme_dpad_set_dir(lv_obj_t *dpad, lv_dir_t dir);

/**
 * @brief Create a HAAT (2-D trackpad) widget.
 *
 * Returns the base container.  A cursor child object is positioned by the
 * caller according to axis values (range -1.0 to +1.0 → pixel offset).
 */
lv_obj_t *sc_ui_theme_draw_axis_haat(lv_obj_t *parent, console_btn_t *cb);

/**
 * @brief Create a throttle (vertical linear axis) widget.
 *
 * Returns the track object.  Call sc_ui_theme_throttle_set_pct() to move grip.
 */
lv_obj_t *sc_ui_theme_draw_axis_throttle(lv_obj_t *parent, console_btn_t *cb);

/** @brief Set throttle position 0–100 %. */
void sc_ui_theme_throttle_set_pct(lv_obj_t *throttle, uint8_t pct);

/**
 * @brief Create a Yaw rotary arc widget.
 *
 * Returns the ring container.  The needle child is rotated by the caller via
 * lv_image_set_angle() in tenths of a degree.
 */
lv_obj_t *sc_ui_theme_draw_axis_yaw(lv_obj_t *parent, console_btn_t *cb);

/**
 * @brief Create a Rudder pedal horizontal axis widget.
 *
 * Returns the track object.  Call sc_ui_theme_rudder_set_pct() to position pedal.
 */
lv_obj_t *sc_ui_theme_draw_axis_rudder(lv_obj_t *parent, console_btn_t *cb);

/** @brief Set rudder pedal position 0–100 % (50 % = centre). */
void sc_ui_theme_rudder_set_pct(lv_obj_t *rudder, uint8_t pct);

/**
 * @brief Create a rotary knob widget (ring + rotatable cap).
 *
 * Returns the ring container.  Rotate the cap child via lv_image_set_angle().
 */
lv_obj_t *sc_ui_theme_draw_knob(lv_obj_t *parent, console_btn_t *cb);

/**
 * @brief Create a jog / shuttle wheel widget (8-frame sprite strip).
 *
 * Returns the base image object.  Call sc_ui_theme_jog_set_angle() to update.
 */
lv_obj_t *sc_ui_theme_draw_jog_wheel(lv_obj_t *parent, console_btn_t *cb);

/**
 * @brief Update jog wheel to the frame matching angle_deg.
 *
 * @param jog       Base object returned by sc_ui_theme_draw_jog_wheel().
 * @param angle_deg Current wheel angle 0–359.
 */
void sc_ui_theme_jog_set_angle(lv_obj_t *jog, uint16_t angle_deg);

/**
 * @brief Create a horizontal slider widget.
 *
 * Returns the track object.  The thumb child object is positioned by LVGL's
 * slider widget internally; set value with lv_slider_set_value().
 */
lv_obj_t *sc_ui_theme_draw_slider_h(lv_obj_t *parent, console_btn_t *cb);

/**
 * @brief Create a vertical slider widget.
 */
lv_obj_t *sc_ui_theme_draw_slider_v(lv_obj_t *parent, console_btn_t *cb);

#ifdef __cplusplus
}
#endif
