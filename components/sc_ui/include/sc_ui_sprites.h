/*
 * sc_ui_sprites.h — Memory-efficient bitmapped sprite atlas system
 *
 * One RGB565 atlas bitmap is loaded per active theme into PSRAM on theme init.
 * All widget sprites are sub-regions of that single buffer; no per-widget SD reads
 * occur after the initial load.
 *
 * Usage:
 *   1. sc_ui_sprites_load() once at theme init → atlas in PSRAM
 *   2. sc_ui_sprites_get(SC_SPRITE_BTN_MOMENTARY_IDLE) → lv_image_dsc_t*
 *   3. lv_obj_set_style_bg_image_src(obj, dsc, 0)  — zero copy, points into atlas
 *   4. sc_ui_sprites_unload() before switching themes
 *
 * Atlas format: raw RGB565 little-endian, row-major, no header.
 * Transparent "no-draw" color key: 0x0001 (near-black, invisible on dark UI).
 *
 * Display: 800 × 1280, ESP32-P4 PSRAM.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Sprite IDs ─────────────────────────────────────────────────────────── */

/**
 * @brief Logical sprite identifier — theme-agnostic.
 *
 * Each ID maps to a rectangle (x,y,w,h) inside the active atlas bitmap.
 * The theme descriptor table (sc_ui_atlas_drake.h / sc_ui_atlas_origin.h)
 * provides the per-theme coordinates.
 */
typedef enum {

    /* ── Buttons ──────────────────────────────────────────────────────── */
    SC_SPRITE_BTN_MOMENTARY_IDLE = 0, /**< Momentary button — unpressed        */
    SC_SPRITE_BTN_MOMENTARY_ARMED,    /**< Momentary button — armed/hover       */
    SC_SPRITE_BTN_MOMENTARY_ACTIVE,   /**< Momentary button — pressed/active    */
    SC_SPRITE_BTN_LATCHING_OFF,       /**< Latching toggle — OFF state          */
    SC_SPRITE_BTN_LATCHING_ON,        /**< Latching toggle — ON state           */
    SC_SPRITE_BTN_INACTIVE,           /**< Any button — greyed / disabled       */
    SC_SPRITE_BTN_DANGER,             /**< Any button — danger/eject/armed-hot  */

    /* ── Sliders ──────────────────────────────────────────────────────── */
    SC_SPRITE_SLIDER_TRACK_H,         /**< Horizontal slider track (stretch-x)  */
    SC_SPRITE_SLIDER_TRACK_V,         /**< Vertical slider track (stretch-y)    */
    SC_SPRITE_SLIDER_THUMB,           /**< Shared thumb knob (both orientations)*/

    /* ── Axes ─────────────────────────────────────────────────────────── */
    SC_SPRITE_AXIS_JOYSTICK_BASE,     /**< Joystick outer gate/bezel            */
    SC_SPRITE_AXIS_JOYSTICK_THUMB,    /**< Joystick thumb circle (positioned)   */
    SC_SPRITE_AXIS_DPAD_BASE,         /**< D-Pad 4-way cross base               */
    SC_SPRITE_AXIS_DPAD_UP,           /**< D-Pad up arrow highlight             */
    SC_SPRITE_AXIS_DPAD_DOWN,         /**< D-Pad down arrow highlight           */
    SC_SPRITE_AXIS_DPAD_LEFT,         /**< D-Pad left arrow highlight           */
    SC_SPRITE_AXIS_DPAD_RIGHT,        /**< D-Pad right arrow highlight          */
    SC_SPRITE_AXIS_HAAT_BASE,         /**< HAAT 2-D trackpad/square base        */
    SC_SPRITE_AXIS_HAAT_CURSOR,       /**< HAAT position cursor dot             */
    SC_SPRITE_AXIS_THROTTLE_TRACK,    /**< Throttle vertical track              */
    SC_SPRITE_AXIS_THROTTLE_GRIP,     /**< Throttle grip/handle                 */
    SC_SPRITE_AXIS_YAW_RING,          /**< Yaw rotary arc ring                  */
    SC_SPRITE_AXIS_YAW_NEEDLE,        /**< Yaw needle (LVGL-rotated by code)    */
    SC_SPRITE_AXIS_RUDDER_TRACK,      /**< Rudder pedal horizontal bar track    */
    SC_SPRITE_AXIS_RUDDER_PEDAL,      /**< Rudder pedal block (translated)      */

    /* ── Knobs ────────────────────────────────────────────────────────── */
    SC_SPRITE_KNOB_RING,              /**< Outer tick ring (static)             */
    SC_SPRITE_KNOB_CAP,               /**< Rotatable cap (LVGL-rotated by code) */

    /* ── Jog Wheel ────────────────────────────────────────────────────── */
    SC_SPRITE_JOG_WHEEL_F0,           /**< Jog wheel frame 0 (0°)              */
    SC_SPRITE_JOG_WHEEL_F1,           /**< Jog wheel frame 1 (45°)             */
    SC_SPRITE_JOG_WHEEL_F2,           /**< Jog wheel frame 2 (90°)             */
    SC_SPRITE_JOG_WHEEL_F3,           /**< Jog wheel frame 3 (135°)            */
    SC_SPRITE_JOG_WHEEL_F4,           /**< Jog wheel frame 4 (180°)            */
    SC_SPRITE_JOG_WHEEL_F5,           /**< Jog wheel frame 5 (225°)            */
    SC_SPRITE_JOG_WHEEL_F6,           /**< Jog wheel frame 6 (270°)            */
    SC_SPRITE_JOG_WHEEL_F7,           /**< Jog wheel frame 7 (315°)            */

    /* ── 9-slice Panel Pieces ─────────────────────────────────────────── */
    SC_SPRITE_PANEL_TL,               /**< Panel corner — top-left              */
    SC_SPRITE_PANEL_TR,               /**< Panel corner — top-right             */
    SC_SPRITE_PANEL_BL,               /**< Panel corner — bottom-left           */
    SC_SPRITE_PANEL_BR,               /**< Panel corner — bottom-right          */
    SC_SPRITE_PANEL_EDGE_T,           /**< Panel top edge (tiled-x)             */
    SC_SPRITE_PANEL_EDGE_B,           /**< Panel bottom edge (tiled-x)          */
    SC_SPRITE_PANEL_EDGE_L,           /**< Panel left edge (tiled-y)            */
    SC_SPRITE_PANEL_EDGE_R,           /**< Panel right edge (tiled-y)           */
    SC_SPRITE_PANEL_CENTER,           /**< Panel center fill (tiled)            */

    SC_SPRITE_COUNT                   /**< Sentinel — must remain last          */
} sc_ui_sprite_id_t;

/* ── Atlas descriptor ────────────────────────────────────────────────────── */

/**
 * @brief Source rectangle for one sprite inside the atlas bitmap.
 *
 * Coordinates are in pixels, origin at top-left of atlas.
 * Width/height must fit within the atlas dimensions.
 */
typedef struct {
    uint16_t x;   /**< Left edge of sprite in atlas      */
    uint16_t y;   /**< Top edge of sprite in atlas       */
    uint16_t w;   /**< Width in pixels                   */
    uint16_t h;   /**< Height in pixels                  */
} sc_ui_sprite_rect_t;

/* ── Atlas metadata ──────────────────────────────────────────────────────── */

/** Overall atlas dimensions (must match the .bin file produced by the packer). */
typedef struct {
    uint16_t width;       /**< Atlas pixel width  (e.g. 512) */
    uint16_t height;      /**< Atlas pixel height (e.g. 512) */
    uint16_t color_depth; /**< Bits per pixel — always 16 (RGB565) */
    uint16_t chroma_key;  /**< Transparent colour value (RGB565) — 0x0001 */
} sc_ui_atlas_meta_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * @brief Load a theme atlas from the SD card into PSRAM.
 *
 * Reads the entire atlas .bin file in one operation (with periodic taskYIELD()
 * to comply with the FreeRTOS watchdog), then builds per-sprite lv_image_dsc_t
 * descriptors that point directly into the PSRAM buffer — no pixel copies.
 *
 * Only one atlas may be resident at a time. Call sc_ui_sprites_unload() first
 * if switching themes.
 *
 * @param atlas_path   VFS path to the raw RGB565 atlas binary
 *                     (e.g. "S:/assets/themes/drake/atlas.bin").
 * @param meta         Atlas dimension/format metadata.
 * @param desc         Array of sprite rectangles, indexed by sc_ui_sprite_id_t.
 *                     Must have exactly SC_SPRITE_COUNT entries.
 * @return ESP_OK on success, ESP_ERR_NO_MEM if PSRAM exhausted,
 *         ESP_ERR_NOT_FOUND if file not found.
 */
esp_err_t sc_ui_sprites_load(const char               *atlas_path,
                              const sc_ui_atlas_meta_t *meta,
                              const sc_ui_sprite_rect_t desc[SC_SPRITE_COUNT]);

/**
 * @brief Unload the current atlas and free its PSRAM buffer.
 *        Safe to call even if no atlas is loaded.
 */
void sc_ui_sprites_unload(void);

/**
 * @brief Retrieve the LVGL image descriptor for a sprite.
 *
 * Returns a pointer into a static descriptor array — do NOT free it.
 * The lv_image_dsc_t::data field points directly into the PSRAM atlas buffer,
 * so the descriptor becomes invalid after sc_ui_sprites_unload().
 *
 * @param id   Logical sprite ID.
 * @return     Pointer to lv_image_dsc_t, or NULL if atlas not loaded or id invalid.
 */
const lv_image_dsc_t *sc_ui_sprites_get(sc_ui_sprite_id_t id);

/**
 * @brief Get the source rectangle for a sprite (for manual sub-image positioning).
 * @param id   Logical sprite ID.
 * @param rect Output rectangle — only written on success.
 * @return ESP_OK or ESP_ERR_INVALID_ARG.
 */
esp_err_t sc_ui_sprites_get_rect(sc_ui_sprite_id_t    id,
                                  sc_ui_sprite_rect_t *rect);

/**
 * @brief Returns true if an atlas is currently loaded in PSRAM.
 */
bool sc_ui_sprites_is_loaded(void);

/**
 * @brief Returns the PSRAM byte size of the currently loaded atlas (0 if none).
 */
size_t sc_ui_sprites_atlas_size(void);

/**
 * @brief Convenience: select the correct jog-wheel frame for a given angle.
 *
 * Maps a 0..359 degree value to one of the 8 frame sprite IDs.
 *
 * @param angle_deg  Current jog-wheel angle in degrees (0–359).
 * @return           SC_SPRITE_JOG_WHEEL_F0 .. SC_SPRITE_JOG_WHEEL_F7
 */
sc_ui_sprite_id_t sc_ui_sprites_jog_frame(uint16_t angle_deg);

/**
 * @brief Pre-calculate and rasterize all needed SVG sprites for the given ship JSON layout.
 *        Reports loading progress using a callback.
 */
esp_err_t sc_ui_sprites_rasterize_all(const char *ship_id, void (*progress_cb)(int pct));

/**
 * @brief Load pre-rasterized per-sprite .bin files from the SD card.
 *
 * Files must have been produced by tools/rasterize_sprites.py and deployed
 * by tools/prep_sdcard.sh.  This avoids on-device SVG rendering entirely.
 *
 * @param theme_dir  VFS path to the theme directory
 *                   (e.g. "/sdcard/assets/themes/drake").
 * @param progress_cb Optional 0–100 progress callback; may be NULL.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if sprites/ directory missing.
 */
esp_err_t sc_ui_sprites_load_from_sdcard(const char *theme_dir,
                                          void (*progress_cb)(int pct));

/**
 * @brief Retrieve a cached rasterized sprite image descriptor.
 *        If w and h are 0, returns the original default size.
 */
const lv_image_dsc_t *sc_ui_sprites_get_scaled(sc_ui_sprite_id_t id, uint16_t w, uint16_t h);

#ifdef __cplusplus
}
#endif
