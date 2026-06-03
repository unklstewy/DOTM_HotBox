#include "sc_ui_theme.h"
#include "lvgl.h"

sc_theme_colors_t sc_theme;

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
    sc_theme.armed    = lv_color_hex(0xFF1F1F); // warn-red
    sc_theme.ready    = lv_color_hex(0xFFB000); // amber
    sc_theme.warn     = lv_color_hex(0x8A3B12); // rust
    sc_theme.off      = lv_color_hex(0x6A5E4E); // bone-dim
    sc_theme.accent   = lv_color_hex(0xFFB000); // amber
    sc_theme.bg       = lv_color_hex(0x000000); // bg-void
    sc_theme.bg_panel = lv_color_hex(0x0B0807); // bg-deck
    sc_theme.text     = lv_color_hex(0xC8B89C); // bone
    sc_theme.text_dim = lv_color_hex(0x6A5E4E); // bone-dim
    sc_theme.divider  = lv_color_hex(0x2A1F18); // chrome
}

void sc_ui_theme_init_origin_lux(void)
{
    sc_theme.armed    = lv_color_hex(0xFF5A6A); // warn-red
    sc_theme.ready    = lv_color_hex(0x6EC4FF); // ice
    sc_theme.warn     = lv_color_hex(0xFFB454); // warn-amber
    sc_theme.off      = lv_color_hex(0x2A6B96); // ice-dim
    sc_theme.accent   = lv_color_hex(0x6EC4FF); // ice
    sc_theme.bg       = lv_color_hex(0x04070D); // bg-night
    sc_theme.bg_panel = lv_color_hex(0x0A1320); // bg-glass
    sc_theme.text     = lv_color_hex(0xE8EEF6); // paper
    sc_theme.text_dim = lv_color_hex(0x7C8AA0); // paper-dim
    sc_theme.divider  = lv_color_hex(0x243349); // bezel
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
