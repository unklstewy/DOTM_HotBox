/*
 * sc_ui_theme.c — Theme palette, atlas load/unload, and widget draw helpers.
 * All sprite access goes through sc_ui_sprites_get(); zero SD reads at draw time.
 */
#include "sc_ui_theme.h"
#include "sc_ui_sprites.h"
#include "sc_ui_atlas_drake.h"
#include "sc_ui_atlas_origin.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "sc_ui_theme";

sc_theme_colors_t sc_theme;
static sc_theme_id_t s_active_theme = SC_THEME_DEFAULT;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/** Apply an atlas sprite as the background image of obj. */
static inline void s_set_sprite(lv_obj_t *obj, sc_ui_sprite_id_t id)
{
    const lv_image_dsc_t *dsc = sc_ui_sprites_get(id);
    if (dsc) lv_obj_set_style_bg_image_src(obj, dsc, 0);
}

/** Apply an atlas sprite as the src of an lv_image object. */
static inline void s_set_img(lv_obj_t *obj, sc_ui_sprite_id_t id)
{
    const lv_image_dsc_t *dsc = sc_ui_sprites_get(id);
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
    sc_ui_sprites_unload();   /* release any previous atlas */

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

    esp_err_t ret = sc_ui_sprites_load(SC_ATLAS_DRAKE_PATH,
                                        &SC_ATLAS_DRAKE_META,
                                        SC_ATLAS_DRAKE_DESC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Drake atlas load failed: %s", esp_err_to_name(ret));
    }
}

void sc_ui_theme_init_origin_lux(void)
{
    sc_ui_sprites_unload();

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

    esp_err_t ret = sc_ui_sprites_load(SC_ATLAS_ORIGIN_PATH,
                                        &SC_ATLAS_ORIGIN_META,
                                        SC_ATLAS_ORIGIN_DESC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Origin atlas load failed: %s", esp_err_to_name(ret));
    }
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

    const lv_image_dsc_t *dsc = sc_ui_sprites_get(id);
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
    s_set_sprite(btn, is_on ? SC_SPRITE_BTN_LATCHING_ON : SC_SPRITE_BTN_LATCHING_OFF);
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

lv_obj_t *sc_ui_theme_draw_axis_joystick(lv_obj_t *parent)
{
    lv_obj_t *base = lv_image_create(parent);
    s_set_img(base, SC_SPRITE_AXIS_JOYSTICK_BASE);
    lv_obj_set_size(base, SC_UI_AXIS_W, SC_UI_AXIS_H);

    lv_obj_t *thumb = lv_image_create(base);
    s_set_img(thumb, SC_SPRITE_AXIS_JOYSTICK_THUMB);
    lv_obj_align(thumb, LV_ALIGN_CENTER, 0, 0);
    return base;
}

lv_obj_t *sc_ui_theme_draw_axis_dpad(lv_obj_t *parent)
{
    lv_obj_t *base = lv_image_create(parent);
    s_set_img(base, SC_SPRITE_AXIS_DPAD_BASE);
    lv_obj_set_size(base, SC_UI_AXIS_W, SC_UI_AXIS_H);

    /* Directional highlight images (hidden by default) */
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
        s_set_img(arrow, dirs[i]);
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

lv_obj_t *sc_ui_theme_draw_axis_haat(lv_obj_t *parent)
{
    lv_obj_t *base = lv_image_create(parent);
    s_set_img(base, SC_SPRITE_AXIS_HAAT_BASE);
    lv_obj_set_size(base, SC_UI_AXIS_W, SC_UI_AXIS_H);

    lv_obj_t *cursor = lv_image_create(base);
    s_set_img(cursor, SC_SPRITE_AXIS_HAAT_CURSOR);
    lv_obj_align(cursor, LV_ALIGN_CENTER, 0, 0);
    return base;
}

lv_obj_t *sc_ui_theme_draw_axis_throttle(lv_obj_t *parent)
{
    lv_obj_t *track = lv_image_create(parent);
    s_set_img(track, SC_SPRITE_AXIS_THROTTLE_TRACK);
    lv_obj_set_size(track, SC_UI_THROTTLE_W, SC_UI_THROTTLE_H);

    lv_obj_t *grip = lv_image_create(track);
    s_set_img(grip, SC_SPRITE_AXIS_THROTTLE_GRIP);
    lv_obj_align(grip, LV_ALIGN_BOTTOM_MID, 0, 0);
    return track;
}

void sc_ui_theme_throttle_set_pct(lv_obj_t *throttle, uint8_t pct)
{
    lv_obj_t *grip = lv_obj_get_child(throttle, 0);
    if (!grip) return;
    /* Map 0–100% to pixel offset: 0% = bottom, 100% = top */
    int range = SC_UI_THROTTLE_H - 20; /* 20 = grip height */
    int ofs   = range - (int)((uint32_t)pct * range / 100u);
    lv_obj_align(grip, LV_ALIGN_TOP_MID, 0, ofs);
}

lv_obj_t *sc_ui_theme_draw_axis_yaw(lv_obj_t *parent)
{
    lv_obj_t *ring = lv_image_create(parent);
    s_set_img(ring, SC_SPRITE_AXIS_YAW_RING);
    lv_obj_set_size(ring, SC_UI_AXIS_W, SC_UI_AXIS_H);

    lv_obj_t *needle = lv_image_create(ring);
    s_set_img(needle, SC_SPRITE_AXIS_YAW_NEEDLE);
    lv_obj_align(needle, LV_ALIGN_CENTER, 0, 0);
    lv_image_set_pivot(needle, 5, 56); /* base of needle */
    return ring;
}

lv_obj_t *sc_ui_theme_draw_axis_rudder(lv_obj_t *parent)
{
    lv_obj_t *track = lv_image_create(parent);
    s_set_img(track, SC_SPRITE_AXIS_RUDDER_TRACK);
    lv_obj_set_size(track, 256, 32);

    lv_obj_t *pedal = lv_image_create(track);
    s_set_img(pedal, SC_SPRITE_AXIS_RUDDER_PEDAL);
    lv_obj_align(pedal, LV_ALIGN_LEFT_MID, 100, 0); /* default centre */
    return track;
}

void sc_ui_theme_rudder_set_pct(lv_obj_t *rudder, uint8_t pct)
{
    lv_obj_t *pedal = lv_obj_get_child(rudder, 0);
    if (!pedal) return;
    /* 0% = left, 50% = centre, 100% = right */
    int range = 256 - 56;
    int x     = (int)((uint32_t)pct * range / 100u);
    lv_obj_align(pedal, LV_ALIGN_LEFT_MID, x, 0);
}

/* ── Knob ─────────────────────────────────────────────────────────────────── */

lv_obj_t *sc_ui_theme_draw_knob(lv_obj_t *parent)
{
    lv_obj_t *ring = lv_image_create(parent);
    s_set_img(ring, SC_SPRITE_KNOB_RING);
    lv_obj_set_size(ring, SC_UI_KNOB_W, SC_UI_KNOB_H);

    lv_obj_t *cap = lv_image_create(ring);
    s_set_img(cap, SC_SPRITE_KNOB_CAP);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, 0);
    lv_image_set_pivot(cap, SC_UI_KNOB_W / 2, SC_UI_KNOB_H / 2);
    return ring;
}

/* ── Jog wheel ───────────────────────────────────────────────────────────── */

lv_obj_t *sc_ui_theme_draw_jog_wheel(lv_obj_t *parent)
{
    lv_obj_t *img = lv_image_create(parent);
    s_set_img(img, SC_SPRITE_JOG_WHEEL_F0);
    lv_obj_set_size(img, SC_UI_JOG_W, SC_UI_JOG_H);
    return img;
}

void sc_ui_theme_jog_set_angle(lv_obj_t *jog, uint16_t angle_deg)
{
    sc_ui_sprite_id_t frame = sc_ui_sprites_jog_frame(angle_deg);
    const lv_image_dsc_t *dsc = sc_ui_sprites_get(frame);
    if (dsc) lv_image_set_src(jog, dsc);
}

/* ── Sliders ─────────────────────────────────────────────────────────────── */

lv_obj_t *sc_ui_theme_draw_slider_h(lv_obj_t *parent)
{
    lv_obj_t *sl = lv_slider_create(parent);
    lv_obj_set_style_bg_image_src(sl, sc_ui_sprites_get(SC_SPRITE_SLIDER_TRACK_H), 0);
    lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sl, 0, 0);

    /* Knob (thumb) */
    lv_obj_set_style_bg_image_src(sl, sc_ui_sprites_get(SC_SPRITE_SLIDER_THUMB),
                                   LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(sl, 0, LV_PART_KNOB);
    return sl;
}

lv_obj_t *sc_ui_theme_draw_slider_v(lv_obj_t *parent)
{
    lv_obj_t *sl = lv_slider_create(parent);
    lv_slider_set_mode(sl, LV_SLIDER_MODE_NORMAL);
    lv_obj_set_style_bg_image_src(sl, sc_ui_sprites_get(SC_SPRITE_SLIDER_TRACK_V), 0);
    lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sl, 0, 0);
    lv_obj_set_style_bg_image_src(sl, sc_ui_sprites_get(SC_SPRITE_SLIDER_THUMB),
                                   LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(sl, 0, LV_PART_KNOB);
    return sl;
}
