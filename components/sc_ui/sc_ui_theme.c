#include "sc_ui_theme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

sc_theme_colors_t sc_theme;
static sc_theme_id_t s_active_theme = SC_THEME_DEFAULT;

/* Cached corner pixel dimensions — populated once at theme init */
static uint32_t s_corner_w = 0;
static uint32_t s_corner_h = 0;
static uint32_t s_edge_t_h = 0;
static uint32_t s_edge_b_h = 0;
static uint32_t s_edge_l_w = 0;
static uint32_t s_edge_r_w = 0;

static void s_cache_theme_dims(const char *tl, const char *et, const char *eb,
                                const char *el, const char *er)
{
    lv_image_header_t hdr;
    s_corner_w = s_corner_h = 0;
    s_edge_t_h = s_edge_b_h = s_edge_l_w = s_edge_r_w = 0;

    if (tl && lv_image_decoder_get_info(tl, &hdr) == LV_RESULT_OK) {
        s_corner_w = hdr.w; s_corner_h = hdr.h;
    }
    if (et && lv_image_decoder_get_info(et, &hdr) == LV_RESULT_OK) s_edge_t_h = hdr.h;
    if (eb && lv_image_decoder_get_info(eb, &hdr) == LV_RESULT_OK) s_edge_b_h = hdr.h;
    if (el && lv_image_decoder_get_info(el, &hdr) == LV_RESULT_OK) s_edge_l_w = hdr.w;
    if (er && lv_image_decoder_get_info(er, &hdr) == LV_RESULT_OK) s_edge_r_w = hdr.w;
}

void sc_ui_theme_init_default(void)
{
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
    s_active_theme = SC_THEME_DRAKE_MILITARY;
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
    /* Pre-read image dimensions once so draw_panel never hits the SD card */
    s_cache_theme_dims(
        "S:/assets/themes/drake/TL.bin",
        "S:/assets/themes/drake/EDGE_T_tile_X.bin",
        "S:/assets/themes/drake/EDGE_B_tile_X.bin",
        "S:/assets/themes/drake/EDGE_L.bin",
        "S:/assets/themes/drake/EDGE_R.bin"
    );
}

void sc_ui_theme_init_origin_lux(void)
{
    s_active_theme = SC_THEME_ORIGIN_LUX;
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
    /* Pre-read image dimensions once so draw_panel never hits the SD card */
    s_cache_theme_dims(
        "S:/assets/themes/origin/TL.bin",
        "S:/assets/themes/origin/EDGE__T.bin",
        "S:/assets/themes/origin/EDGE__B.bin",
        NULL,  /* origin has no left/right edge images */
        NULL
    );
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

lv_obj_t *sc_ui_theme_draw_panel(lv_obj_t *panel)
{
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    const char *tl = NULL, *tr = NULL, *bl = NULL, *br = NULL;
    const char *et = NULL, *eb = NULL, *el = NULL, *er = NULL, *cen = NULL;
    
    if (s_active_theme == SC_THEME_DRAKE_MILITARY) {
        tl = "S:/assets/themes/drake/TL.bin"; tr = "S:/assets/themes/drake/TR.bin";
        bl = "S:/assets/themes/drake/BL.bin"; br = "S:/assets/themes/drake/BR.bin";
        et = "S:/assets/themes/drake/EDGE_T_tile_X.bin"; eb = "S:/assets/themes/drake/EDGE_B_tile_X.bin";
        el = "S:/assets/themes/drake/EDGE_L.bin"; er = "S:/assets/themes/drake/EDGE_R.bin";
        cen = "S:/assets/themes/drake/CENTER_stretch.bin";
    } else {
        tl = "S:/assets/themes/origin/TL.bin"; tr = "S:/assets/themes/origin/TR.bin";
        bl = "S:/assets/themes/origin/BL.bin"; br = "S:/assets/themes/origin/BR.bin";
        et = "S:/assets/themes/origin/EDGE__T.bin"; eb = "S:/assets/themes/origin/EDGE__B.bin";
        el = NULL; er = NULL;
        cen = "S:/assets/themes/origin/CENTER.bin";
    }

    /* Get corner/edge sizes from cache — no SD card reads here */
    uint32_t pad_x = (s_corner_w > 0) ? s_corner_w : 16;
    uint32_t pad_y = (s_corner_h > 0) ? s_corner_h : 16;

    /* CENTER */
    if (cen) {
        lv_obj_t *c = lv_obj_create(panel);
        lv_obj_set_style_bg_image_src(c, cen, 0);
        lv_obj_set_style_bg_image_tiled(c, true, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_size(c, LV_PCT(100), LV_PCT(100));
        lv_obj_align(c, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_t *c = lv_obj_create(panel);
        lv_obj_set_style_bg_color(c, lv_color_hex(0x140E0A), 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_size(c, LV_PCT(100), LV_PCT(100));
        lv_obj_align(c, LV_ALIGN_CENTER, 0, 0);
    }

    taskYIELD();  /* give IDLE a chance between construction phases */

    /* EDGES */
    if (et && s_edge_t_h > 0) {
        lv_obj_t *t = lv_obj_create(panel);
        lv_obj_set_style_bg_image_src(t, et, 0);
        lv_obj_set_style_bg_image_tiled(t, true, 0);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_size(t, LV_PCT(100), s_edge_t_h);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);
    }
    if (eb && s_edge_b_h > 0) {
        lv_obj_t *b = lv_obj_create(panel);
        lv_obj_set_style_bg_image_src(b, eb, 0);
        lv_obj_set_style_bg_image_tiled(b, true, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_size(b, LV_PCT(100), s_edge_b_h);
        lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    if (el && s_edge_l_w > 0) {
        lv_obj_t *l = lv_obj_create(panel);
        lv_obj_set_style_bg_image_src(l, el, 0);
        lv_obj_set_style_bg_image_tiled(l, true, 0);
        lv_obj_set_style_border_width(l, 0, 0);
        lv_obj_set_size(l, s_edge_l_w, LV_PCT(100));
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    }
    if (er && s_edge_r_w > 0) {
        lv_obj_t *r = lv_obj_create(panel);
        lv_obj_set_style_bg_image_src(r, er, 0);
        lv_obj_set_style_bg_image_tiled(r, true, 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_size(r, s_edge_r_w, LV_PCT(100));
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    taskYIELD();  /* give IDLE a chance between construction phases */

    /* CORNERS */
    if (tl) {
        lv_obj_t *c = lv_image_create(panel);
        lv_image_set_src(c, tl);
        lv_obj_align(c, LV_ALIGN_TOP_LEFT, 0, 0);
    }
    if (tr) {
        lv_obj_t *c = lv_image_create(panel);
        lv_image_set_src(c, tr);
        lv_obj_align(c, LV_ALIGN_TOP_RIGHT, 0, 0);
    }
    if (bl) {
        lv_obj_t *c = lv_image_create(panel);
        lv_image_set_src(c, bl);
        lv_obj_align(c, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    if (br) {
        lv_obj_t *c = lv_image_create(panel);
        lv_image_set_src(c, br);
        lv_obj_align(c, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

    /* Inner content container */
    lv_obj_t *content = lv_obj_create(panel);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(content, pad_x, 0);
    lv_obj_set_style_pad_right(content, pad_x, 0);
    lv_obj_set_style_pad_top(content, pad_y, 0);
    lv_obj_set_style_pad_bottom(content, pad_y, 0);
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 0);

    return content;
}

void sc_ui_theme_style_btn(lv_obj_t *btn, lv_color_t state_color)
{
    /* Reset some potential conflicts before styling */
    lv_obj_set_style_bg_color(btn, state_color, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 0, 0);

    bool is_armed = (lv_color_to_int(state_color) == lv_color_to_int(sc_theme.armed));
    bool is_active = (lv_color_to_int(state_color) == lv_color_to_int(sc_theme.ready) || 
                      lv_color_to_int(state_color) == lv_color_to_int(sc_theme.warn) || 
                      lv_color_to_int(state_color) == lv_color_to_int(sc_theme.accent));

    if (s_active_theme == SC_THEME_DRAKE_MILITARY) {
        if (!is_active && !is_armed) {
            lv_obj_set_style_bg_image_src(btn, "S:/assets/themes/drake/btn_idle.bin", 0);
        } else if (is_armed) {
            lv_obj_set_style_bg_image_src(btn, "S:/assets/themes/drake/btn_armed.bin", 0);
        } else {
            lv_obj_set_style_bg_image_src(btn, "S:/assets/themes/drake/btn_active.bin", 0);
            /* Tint the active button with the state color */
            lv_obj_set_style_bg_image_recolor(btn, state_color, 0);
            lv_obj_set_style_bg_image_recolor_opa(btn, LV_OPA_30, 0);
        }
    } else if (s_active_theme == SC_THEME_ORIGIN_LUX) {
        if (!is_active && !is_armed) {
            lv_obj_set_style_bg_image_src(btn, "S:/assets/themes/origin/btn_idle.bin", 0);
        } else if (is_armed) {
            lv_obj_set_style_bg_image_src(btn, "S:/assets/themes/origin/btn_hover.bin", 0);
        } else {
            lv_obj_set_style_bg_image_src(btn, "S:/assets/themes/origin/btn_active.bin", 0);
            lv_obj_set_style_bg_image_recolor(btn, state_color, 0);
            lv_obj_set_style_bg_image_recolor_opa(btn, LV_OPA_30, 0);
        }
    }
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
}

void sc_ui_theme_style_tab(lv_obj_t *tab, bool is_active)
{
    /* Common resets */
    lv_obj_set_style_shadow_width(tab, 0, 0);
    lv_obj_set_style_outline_width(tab, 0, 0);
    lv_obj_set_style_border_width(tab, 0, 0);
    
    if (is_active) {
        lv_obj_set_style_bg_color(tab, sc_theme.accent, 0);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
        /* Assuming the label is the first child */
        if (lv_obj_get_child_cnt(tab) > 0) {
            lv_obj_set_style_text_color(lv_obj_get_child(tab, 0), lv_color_hex(0x000000), 0);
        }
    } else {
        lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
        if (lv_obj_get_child_cnt(tab) > 0) {
            lv_obj_set_style_text_color(lv_obj_get_child(tab, 0), sc_theme.text_dim, 0);
        }
    }

    if (s_active_theme == SC_THEME_DRAKE_MILITARY) {
        lv_obj_set_style_radius(tab, 0, 0);
        if (!is_active) {
            lv_obj_set_style_outline_color(tab, lv_color_hex(0x3A2C20), 0);
            lv_obj_set_style_outline_width(tab, 1, 0);
        }
    } else if (s_active_theme == SC_THEME_ORIGIN_LUX) {
        lv_obj_set_style_radius(tab, 6, 0);
    }
}
