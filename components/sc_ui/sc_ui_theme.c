/*
 * sc_ui_theme.c — Theme palette and widget drawing helper factories.
 * Queries and resolves custom scaled sprites from the boot-time vector cache.
 */
#include "sc_ui_theme.h"
#include "sc_ui_sprites.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "sc_ui_theme";

sc_theme_colors_t sc_theme;
static sc_theme_id_t s_active_theme = SC_THEME_DEFAULT;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/** Apply a sprite as the background image of obj, scaling it to object bounds if possible. */
static inline void s_set_sprite(lv_obj_t *obj, sc_ui_sprite_id_t id)
{
    console_btn_t *cb = lv_obj_get_user_data(obj);
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;
    const lv_image_dsc_t *dsc = sc_ui_sprites_get_scaled(id, w, h);
    if (dsc) lv_obj_set_style_bg_image_src(obj, dsc, 0);
}

/** Apply a sprite as the src of an lv_image object, scaling it if possible. */
static inline void s_set_img(lv_obj_t *obj, sc_ui_sprite_id_t id)
{
    console_btn_t *cb = lv_obj_get_user_data(obj);
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;
    const lv_image_dsc_t *dsc = sc_ui_sprites_get_scaled(id, w, h);
    if (dsc) lv_image_set_src(obj, dsc);
}

/* ── Palette inits ───────────────────────────────────────────────────────── */

void sc_ui_theme_init_default(void)
{
    s_active_theme    = SC_THEME_DEFAULT;
    sc_theme.armed    = lv_color_hex(0xFF4444);
    sc_theme.ready    = lv_color_hex(0x00FF88);
    sc_theme.warn     = lv_color_hex(0xFFAA00);
    sc_theme.off      = lv_color_hex(0x333333);
    sc_theme.accent   = lv_color_hex(0x0099FF);
    sc_theme.bg       = lv_color_hex(0x111111);
    sc_theme.bg_panel = lv_color_hex(0x1A1A2E);
    sc_theme.text     = lv_color_hex(0xE0E0E0);
    sc_theme.text_dim = lv_color_hex(0x888888);
    sc_theme.divider  = lv_color_hex(0x2A2A3E);
}

void sc_ui_theme_init_drake_military(void)
{
    s_active_theme    = SC_THEME_DRAKE_MILITARY;
    sc_theme.armed    = lv_color_hex(0xFF1F1F);
    sc_theme.ready    = lv_color_hex(0xFFB000);
    sc_theme.warn     = lv_color_hex(0x8A3B12);
    sc_theme.off      = lv_color_hex(0x6A5E4E);
    sc_theme.accent   = lv_color_hex(0xFFB000);
    sc_theme.bg       = lv_color_hex(0x000000);
    sc_theme.bg_panel = lv_color_hex(0x0B0807);
    sc_theme.text     = lv_color_hex(0xC8B89C);
    sc_theme.text_dim = lv_color_hex(0x6A5E4E);
    sc_theme.divider  = lv_color_hex(0x2A1F18);
}

void sc_ui_theme_init_origin_lux(void)
{
    s_active_theme    = SC_THEME_ORIGIN_LUX;
    sc_theme.armed    = lv_color_hex(0xFF5A6A);
    sc_theme.ready    = lv_color_hex(0x6EC4FF);
    sc_theme.warn     = lv_color_hex(0xFFB454);
    sc_theme.off      = lv_color_hex(0x2A6B96);
    sc_theme.accent   = lv_color_hex(0x6EC4FF);
    sc_theme.bg       = lv_color_hex(0x04070D);
    sc_theme.bg_panel = lv_color_hex(0x0A1320);
    sc_theme.text     = lv_color_hex(0xE8EEF6);
    sc_theme.text_dim = lv_color_hex(0x7C8AA0);
    sc_theme.divider  = lv_color_hex(0x243349);
}

void sc_ui_theme_set(const sc_theme_colors_t *colors)
{
    if (colors) {
        lv_lock();
        sc_theme = *colors;
        lv_obj_report_style_change(NULL);
        lv_unlock();
    }
}

sc_theme_id_t sc_ui_theme_get_active(void) { return s_active_theme; }

/* ── Panel ───────────────────────────────────────────────────────────────── */

lv_obj_t *sc_ui_theme_draw_panel(lv_obj_t *panel)
{
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Determine inset from sprite rect dimensions */
    sc_ui_sprite_rect_t r_tl = {0};
    sc_ui_sprites_get_rect(SC_SPRITE_PANEL_TL, &r_tl);
    uint32_t pad_x = r_tl.w > 0 ? r_tl.w : 16;
    uint32_t pad_y = r_tl.h > 0 ? r_tl.h : 16;

    /* Center fill */
    lv_obj_t *c = lv_obj_create(panel);
    lv_obj_set_size(c, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, 0);
    s_set_sprite(c, SC_SPRITE_PANEL_CENTER);
    lv_obj_set_style_bg_image_tiled(c, true, 0);

    taskYIELD();

    /* Edges */
    sc_ui_sprite_rect_t r = {0};

    sc_ui_sprites_get_rect(SC_SPRITE_PANEL_EDGE_T, &r);
    if (r.h > 0) {
        lv_obj_t *t = lv_obj_create(panel);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_size(t, LV_PCT(100), r.h);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);
        s_set_sprite(t, SC_SPRITE_PANEL_EDGE_T);
        lv_obj_set_style_bg_image_tiled(t, true, 0);
    }

    sc_ui_sprites_get_rect(SC_SPRITE_PANEL_EDGE_B, &r);
    if (r.h > 0) {
        lv_obj_t *b = lv_obj_create(panel);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_size(b, LV_PCT(100), r.h);
        lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, 0);
        s_set_sprite(b, SC_SPRITE_PANEL_EDGE_B);
        lv_obj_set_style_bg_image_tiled(b, true, 0);
    }

    sc_ui_sprites_get_rect(SC_SPRITE_PANEL_EDGE_L, &r);
    if (r.w > 0) {
        lv_obj_t *l = lv_obj_create(panel);
        lv_obj_set_style_border_width(l, 0, 0);
        lv_obj_set_size(l, r.w, LV_PCT(100));
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        s_set_sprite(l, SC_SPRITE_PANEL_EDGE_L);
        lv_obj_set_style_bg_image_tiled(l, true, 0);
    }

    sc_ui_sprites_get_rect(SC_SPRITE_PANEL_EDGE_R, &r);
    if (r.w > 0) {
        lv_obj_t *rv = lv_obj_create(panel);
        lv_obj_set_style_border_width(rv, 0, 0);
        lv_obj_set_size(rv, r.w, LV_PCT(100));
        lv_obj_align(rv, LV_ALIGN_RIGHT_MID, 0, 0);
        s_set_sprite(rv, SC_SPRITE_PANEL_EDGE_R);
        lv_obj_set_style_bg_image_tiled(rv, true, 0);
    }

    taskYIELD();

    /* Corners */
    struct { sc_ui_sprite_id_t id; lv_align_t align; } corners[] = {
        { SC_SPRITE_PANEL_TL, LV_ALIGN_TOP_LEFT    },
        { SC_SPRITE_PANEL_TR, LV_ALIGN_TOP_RIGHT   },
        { SC_SPRITE_PANEL_BL, LV_ALIGN_BOTTOM_LEFT },
        { SC_SPRITE_PANEL_BR, LV_ALIGN_BOTTOM_RIGHT},
    };
    for (int i = 0; i < 4; i++) {
        const lv_image_dsc_t *dsc = sc_ui_sprites_get(corners[i].id);
        if (dsc && dsc->header.w > 0) {
            lv_obj_t *img = lv_image_create(panel);
            lv_image_set_src(img, dsc);
            lv_obj_align(img, corners[i].align, 0, 0);
        }
    }

    /* Inner content container */
    lv_obj_t *content = lv_obj_create(panel);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(content, (lv_coord_t)pad_x, 0);
    lv_obj_set_style_pad_right(content, (lv_coord_t)pad_x, 0);
    lv_obj_set_style_pad_top(content, (lv_coord_t)pad_y, 0);
    lv_obj_set_style_pad_bottom(content, (lv_coord_t)pad_y, 0);
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 0);
    return content;
}

/* ── Button styling ──────────────────────────────────────────────────────── */

void sc_ui_theme_style_btn(lv_obj_t *btn, lv_color_t state_color)
{
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);

    bool is_armed  = lv_color_to_int(state_color) == lv_color_to_int(sc_theme.armed);
    bool is_active = !is_armed &&
                     (lv_color_to_int(state_color) == lv_color_to_int(sc_theme.ready)  ||
                      lv_color_to_int(state_color) == lv_color_to_int(sc_theme.warn)   ||
                      lv_color_to_int(state_color) == lv_color_to_int(sc_theme.accent));

    sc_ui_sprite_id_t id = SC_SPRITE_BTN_MOMENTARY_IDLE;
    if (is_armed)       id = SC_SPRITE_BTN_DANGER;
    else if (is_active) id = SC_SPRITE_BTN_MOMENTARY_ACTIVE;

    console_btn_t *cb = lv_obj_get_user_data(btn);
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;

    const lv_image_dsc_t *dsc = sc_ui_sprites_get_scaled(id, w, h);
    if (dsc) {
        lv_obj_set_style_bg_image_src(btn, dsc, 0);
        if (is_active) {
            lv_obj_set_style_bg_image_recolor(btn, state_color, 0);
            lv_obj_set_style_bg_image_recolor_opa(btn, LV_OPA_30, 0);
        }
    }
}

void sc_ui_theme_style_btn_latching(lv_obj_t *btn, bool is_on)
{
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);

    sc_ui_sprite_id_t id = is_on ? SC_SPRITE_BTN_LATCHING_ON : SC_SPRITE_BTN_LATCHING_OFF;

    console_btn_t *cb = lv_obj_get_user_data(btn);
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;

    const lv_image_dsc_t *dsc = sc_ui_sprites_get_scaled(id, w, h);
    if (dsc) {
        lv_obj_set_style_bg_image_src(btn, dsc, 0);
    }
}

void sc_ui_theme_style_tab(lv_obj_t *tab, bool is_active)
{
    lv_obj_set_style_shadow_width(tab, 0, 0);
    lv_obj_set_style_outline_width(tab, 0, 0);
    lv_obj_set_style_border_width(tab, 0, 0);

    if (is_active) {
        lv_obj_set_style_bg_color(tab, sc_theme.accent, 0);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
        if (lv_obj_get_child_cnt(tab) > 0)
            lv_obj_set_style_text_color(lv_obj_get_child(tab, 0),
                                        lv_color_hex(0x000000), 0);
    } else {
        lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
        if (lv_obj_get_child_cnt(tab) > 0)
            lv_obj_set_style_text_color(lv_obj_get_child(tab, 0),
                                        sc_theme.text_dim, 0);
    }
    lv_obj_set_style_radius(tab,
        s_active_theme == SC_THEME_ORIGIN_LUX ? 6 : 0, 0);
}

/* ── Axis widgets ────────────────────────────────────────────────────────── */

lv_obj_t *sc_ui_theme_draw_axis_joystick(lv_obj_t *parent, console_btn_t *cb)
{
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;

    uint16_t base_w = w > 0 ? w : SC_UI_AXIS_W;
    uint16_t base_h = h > 0 ? h : SC_UI_AXIS_H;

    double scale_x = (double)base_w / 120.0;
    double scale_y = (double)base_h / 120.0;
    uint16_t thumb_w = (uint16_t)(40.0 * scale_x + 0.5);
    uint16_t thumb_h = (uint16_t)(40.0 * scale_y + 0.5);

    lv_obj_t *base = lv_image_create(parent);
    if (cb) lv_obj_set_user_data(base, cb);
    lv_image_set_src(base, sc_ui_sprites_get_scaled(SC_SPRITE_AXIS_JOYSTICK_BASE, base_w, base_h));
    lv_obj_set_size(base, base_w, base_h);

    lv_obj_t *thumb = lv_image_create(base);
    lv_image_set_src(thumb, sc_ui_sprites_get_scaled(SC_SPRITE_AXIS_JOYSTICK_THUMB, thumb_w, thumb_h));
    lv_obj_align(thumb, LV_ALIGN_CENTER, 0, 0);
    return base;
}

lv_obj_t *sc_ui_theme_draw_axis_dpad(lv_obj_t *parent, console_btn_t *cb)
{
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;

    uint16_t base_w = w > 0 ? w : SC_UI_AXIS_W;
    uint16_t base_h = h > 0 ? h : SC_UI_AXIS_H;

    double scale_x = (double)base_w / 120.0;
    double scale_y = (double)base_h / 120.0;
    uint16_t arrow_w = (uint16_t)(40.0 * scale_x + 0.5);
    uint16_t arrow_h = (uint16_t)(36.0 * scale_y + 0.5);

    lv_obj_t *base = lv_image_create(parent);
    if (cb) lv_obj_set_user_data(base, cb);
    lv_image_set_src(base, sc_ui_sprites_get_scaled(SC_SPRITE_AXIS_DPAD_BASE, base_w, base_h));
    lv_obj_set_size(base, base_w, base_h);

    sc_ui_sprite_id_t dirs[] = {
        SC_SPRITE_AXIS_DPAD_UP, SC_SPRITE_AXIS_DPAD_DOWN,
        SC_SPRITE_AXIS_DPAD_LEFT, SC_SPRITE_AXIS_DPAD_RIGHT
    };
    lv_align_t aligns[] = {
        LV_ALIGN_TOP_MID, LV_ALIGN_BOTTOM_MID,
        LV_ALIGN_LEFT_MID, LV_ALIGN_RIGHT_MID
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *arrow = lv_image_create(base);
        lv_image_set_src(arrow, sc_ui_sprites_get_scaled(dirs[i], arrow_w, arrow_h));
        lv_obj_align(arrow, aligns[i], 0, 0);
        lv_obj_add_flag(arrow, LV_OBJ_FLAG_HIDDEN);
    }
    return base;
}

void sc_ui_theme_dpad_set_dir(lv_obj_t *dpad, lv_dir_t dir)
{
    /* Children: 0=up, 1=down, 2=left, 3=right */
    lv_dir_t map[] = { LV_DIR_TOP, LV_DIR_BOTTOM, LV_DIR_LEFT, LV_DIR_RIGHT };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *child = lv_obj_get_child(dpad, i);
        if (!child) continue;
        if (dir & map[i]) lv_obj_remove_flag(child, LV_OBJ_FLAG_HIDDEN);
        else              lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *sc_ui_theme_draw_axis_haat(lv_obj_t *parent, console_btn_t *cb)
{
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;

    uint16_t base_w = w > 0 ? w : SC_UI_AXIS_W;
    uint16_t base_h = h > 0 ? h : SC_UI_AXIS_H;

    double scale_x = (double)base_w / 120.0;
    double scale_y = (double)base_h / 120.0;
    uint16_t cursor_w = (uint16_t)(24.0 * scale_x + 0.5);
    uint16_t cursor_h = (uint16_t)(24.0 * scale_y + 0.5);

    lv_obj_t *base = lv_image_create(parent);
    if (cb) lv_obj_set_user_data(base, cb);
    lv_image_set_src(base, sc_ui_sprites_get_scaled(SC_SPRITE_AXIS_HAAT_BASE, base_w, base_h));
    lv_obj_set_size(base, base_w, base_h);

    lv_obj_t *cursor = lv_image_create(base);
    lv_image_set_src(cursor, sc_ui_sprites_get_scaled(SC_SPRITE_AXIS_HAAT_CURSOR, cursor_w, cursor_h));
    lv_obj_align(cursor, LV_ALIGN_CENTER, 0, 0);
    return base;
}

lv_obj_t *sc_ui_theme_draw_axis_throttle(lv_obj_t *parent, console_btn_t *cb)
{
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;

    uint16_t track_w = w > 0 ? w : SC_UI_THROTTLE_W;
    uint16_t track_h = h > 0 ? h : SC_UI_THROTTLE_H;

    double scale_x = (double)track_w / 44.0;
    double scale_y = (double)track_h / 120.0;
    uint16_t grip_w = (uint16_t)(60.0 * scale_x + 0.5);
    uint16_t grip_h = (uint16_t)(20.0 * scale_y + 0.5);

    lv_obj_t *track = lv_image_create(parent);
    if (cb) lv_obj_set_user_data(track, cb);
    lv_image_set_src(track, sc_ui_sprites_get_scaled(SC_SPRITE_AXIS_THROTTLE_TRACK, track_w, track_h));
    lv_obj_set_size(track, track_w, track_h);

    lv_obj_t *grip = lv_image_create(track);
    lv_image_set_src(grip, sc_ui_sprites_get_scaled(SC_SPRITE_AXIS_THROTTLE_GRIP, grip_w, grip_h));
    lv_obj_align(grip, LV_ALIGN_BOTTOM_MID, 0, 0);
    return track;
}

void sc_ui_theme_throttle_set_pct(lv_obj_t *throttle, uint8_t pct)
{
    lv_obj_t *grip = lv_obj_get_child(throttle, 0);
    if (!grip) return;
    
    // Get track dimensions
    lv_coord_t track_h = lv_obj_get_height(throttle);
    lv_coord_t grip_h = lv_obj_get_height(grip);
    if (track_h <= 0) track_h = SC_UI_THROTTLE_H;
    if (grip_h <= 0) grip_h = 20;

    int range = track_h - grip_h;
    int ofs   = range - (int)((uint32_t)pct * range / 100u);
    lv_obj_align(grip, LV_ALIGN_TOP_MID, 0, ofs);
}

lv_obj_t *sc_ui_theme_draw_axis_yaw(lv_obj_t *parent, console_btn_t *cb)
{
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;

    uint16_t base_w = w > 0 ? w : SC_UI_AXIS_W;
    uint16_t base_h = h > 0 ? h : SC_UI_AXIS_H;

    double scale_x = (double)base_w / 120.0;
    double scale_y = (double)base_h / 120.0;
    uint16_t needle_w = (uint16_t)(10.0 * scale_x + 0.5);
    uint16_t needle_h = (uint16_t)(56.0 * scale_y + 0.5);

    lv_obj_t *ring = lv_image_create(parent);
    if (cb) lv_obj_set_user_data(ring, cb);
    lv_image_set_src(ring, sc_ui_sprites_get_scaled(SC_SPRITE_AXIS_YAW_RING, base_w, base_h));
    lv_obj_set_size(ring, base_w, base_h);

    lv_obj_t *needle = lv_image_create(ring);
    lv_image_set_src(needle, sc_ui_sprites_get_scaled(SC_SPRITE_AXIS_YAW_NEEDLE, needle_w, needle_h));
    lv_obj_align(needle, LV_ALIGN_CENTER, 0, 0);
    lv_image_set_pivot(needle, needle_w / 2, needle_h);
    return ring;
}

lv_obj_t *sc_ui_theme_draw_axis_rudder(lv_obj_t *parent, console_btn_t *cb)
{
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;

    uint16_t track_w = w > 0 ? w : 256;
    uint16_t track_h = h > 0 ? h : 32;

    double scale_x = (double)track_w / 256.0;
    double scale_y = (double)track_h / 32.0;
    uint16_t pedal_w = (uint16_t)(56.0 * scale_x + 0.5);
    uint16_t pedal_h = (uint16_t)(40.0 * scale_y + 0.5);

    lv_obj_t *track = lv_image_create(parent);
    if (cb) lv_obj_set_user_data(track, cb);
    lv_image_set_src(track, sc_ui_sprites_get_scaled(SC_SPRITE_AXIS_RUDDER_TRACK, track_w, track_h));
    lv_obj_set_size(track, track_w, track_h);

    lv_obj_t *pedal = lv_image_create(track);
    lv_image_set_src(pedal, sc_ui_sprites_get_scaled(SC_SPRITE_AXIS_RUDDER_PEDAL, pedal_w, pedal_h));
    lv_obj_align(pedal, LV_ALIGN_LEFT_MID, (track_w - pedal_w) / 2, 0);
    return track;
}

void sc_ui_theme_rudder_set_pct(lv_obj_t *rudder, uint8_t pct)
{
    lv_obj_t *pedal = lv_obj_get_child(rudder, 0);
    if (!pedal) return;
    
    // Get dimensions
    lv_coord_t track_w = lv_obj_get_width(rudder);
    lv_coord_t pedal_w = lv_obj_get_width(pedal);
    if (track_w <= 0) track_w = 256;
    if (pedal_w <= 0) pedal_w = 56;

    int range = track_w - pedal_w;
    int x     = (int)((uint32_t)pct * range / 100u);
    lv_obj_align(pedal, LV_ALIGN_LEFT_MID, x, 0);
}

/* ── Knob ─────────────────────────────────────────────────────────────────── */

lv_obj_t *sc_ui_theme_draw_knob(lv_obj_t *parent, console_btn_t *cb)
{
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;

    uint16_t base_w = w > 0 ? w : SC_UI_KNOB_W;
    uint16_t base_h = h > 0 ? h : SC_UI_KNOB_H;

    lv_obj_t *ring = lv_image_create(parent);
    if (cb) lv_obj_set_user_data(ring, cb);
    lv_image_set_src(ring, sc_ui_sprites_get_scaled(SC_SPRITE_KNOB_RING, base_w, base_h));
    lv_obj_set_size(ring, base_w, base_h);

    lv_obj_t *cap = lv_image_create(ring);
    lv_image_set_src(cap, sc_ui_sprites_get_scaled(SC_SPRITE_KNOB_CAP, base_w, base_h));
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, 0);
    lv_image_set_pivot(cap, base_w / 2, base_h / 2);
    return ring;
}

/* ── Jog wheel ───────────────────────────────────────────────────────────── */

lv_obj_t *sc_ui_theme_draw_jog_wheel(lv_obj_t *parent, console_btn_t *cb)
{
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;

    uint16_t base_w = w > 0 ? w : SC_UI_JOG_W;
    uint16_t base_h = h > 0 ? h : SC_UI_JOG_H;

    lv_obj_t *img = lv_image_create(parent);
    if (cb) lv_obj_set_user_data(img, cb);
    lv_image_set_src(img, sc_ui_sprites_get_scaled(SC_SPRITE_JOG_WHEEL_F0, base_w, base_h));
    lv_obj_set_size(img, base_w, base_h);
    return img;
}

void sc_ui_theme_jog_set_angle(lv_obj_t *jog, uint16_t angle_deg)
{
    console_btn_t *cb = lv_obj_get_user_data(jog);
    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;
    
    sc_ui_sprite_id_t frame = sc_ui_sprites_jog_frame(angle_deg);
    const lv_image_dsc_t *dsc = sc_ui_sprites_get_scaled(frame, w, h);
    if (dsc) lv_image_set_src(jog, dsc);
}

/* ── Sliders ─────────────────────────────────────────────────────────────── */

lv_obj_t *sc_ui_theme_draw_slider_h(lv_obj_t *parent, console_btn_t *cb)
{
    lv_obj_t *sl = lv_slider_create(parent);
    if (cb) lv_obj_set_user_data(sl, cb);

    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;

    uint16_t track_w = w > 0 ? w : 120;
    uint16_t track_h = h > 0 ? h : 24;

    double scale_x = (double)track_w / 120.0;
    double scale_y = (double)track_h / 24.0;
    uint16_t thumb_w = (uint16_t)(40.0 * scale_x + 0.5);
    uint16_t thumb_h = (uint16_t)(24.0 * scale_y + 0.5);

    lv_obj_set_style_bg_image_src(sl, sc_ui_sprites_get_scaled(SC_SPRITE_SLIDER_TRACK_H, track_w, track_h), 0);
    lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sl, 0, 0);

    lv_obj_set_style_bg_image_src(sl, sc_ui_sprites_get_scaled(SC_SPRITE_SLIDER_THUMB, thumb_w, thumb_h), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(sl, 0, LV_PART_KNOB);
    return sl;
}

lv_obj_t *sc_ui_theme_draw_slider_v(lv_obj_t *parent, console_btn_t *cb)
{
    lv_obj_t *sl = lv_slider_create(parent);
    if (cb) lv_obj_set_user_data(sl, cb);
    lv_slider_set_mode(sl, LV_SLIDER_MODE_NORMAL);

    uint16_t w = cb ? cb->pixel_w : 0;
    uint16_t h = cb ? cb->pixel_h : 0;

    uint16_t track_w = w > 0 ? w : 24;
    uint16_t track_h = h > 0 ? h : 120;

    double scale_x = (double)track_w / 24.0;
    double scale_y = (double)track_h / 120.0;
    uint16_t thumb_w = (uint16_t)(24.0 * scale_x + 0.5);
    uint16_t thumb_h = (uint16_t)(40.0 * scale_y + 0.5);

    lv_obj_set_style_bg_image_src(sl, sc_ui_sprites_get_scaled(SC_SPRITE_SLIDER_TRACK_V, track_w, track_h), 0);
    lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sl, 0, 0);

    lv_obj_set_style_bg_image_src(sl, sc_ui_sprites_get_scaled(SC_SPRITE_SLIDER_THUMB, thumb_w, thumb_h), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(sl, 0, LV_PART_KNOB);
    return sl;
}
