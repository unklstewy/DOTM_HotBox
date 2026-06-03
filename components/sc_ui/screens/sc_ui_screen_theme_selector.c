#include "sc_ui_screen_theme_selector.h"
#include "sc_ui_theme.h"
#include "sc_ui_screens.h"
#include "esp_log.h"

static const char *TAG = "sc_ui_theme_sel";

static void btn_drake_event_cb(lv_event_t * e) {
    sc_ui_router_push(SC_UI_SCREEN_DRAKE);
}

static void btn_origin_event_cb(lv_event_t * e) {
    sc_ui_router_push(SC_UI_SCREEN_ORIGIN);
}

static void btn_back_event_cb(lv_event_t * e) {
    sc_ui_router_pop();
}

lv_obj_t *sc_ui_screen_theme_selector_create(lv_obj_t *parent) {
    lv_lock();
    sc_ui_theme_init_default(); // Reset to default theme

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scr, SC_COL_BG, 0);

    lv_obj_t *lbl_title = lv_label_create(scr);
    lv_label_set_text(lbl_title, "THEME SELECTOR");
    lv_obj_set_style_text_color(lbl_title, SC_COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_title, SC_FONT_MEDIUM, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 100);

    // Drake Button
    lv_obj_t *btn_drake = lv_button_create(scr);
    lv_obj_set_size(btn_drake, 400, 100);
    lv_obj_align(btn_drake, LV_ALIGN_CENTER, 0, -100);
    lv_obj_set_style_bg_color(btn_drake, lv_color_hex(0xFFB000), 0); // Drake Amber
    lv_obj_add_event_cb(btn_drake, btn_drake_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_drake = lv_label_create(btn_drake);
    lv_label_set_text(lbl_drake, "DRAKE MILITARY");
    lv_obj_set_style_text_color(lbl_drake, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_drake, SC_FONT_MEDIUM, 0);
    lv_obj_align(lbl_drake, LV_ALIGN_CENTER, 0, 0);

    // Origin Button
    lv_obj_t *btn_origin = lv_button_create(scr);
    lv_obj_set_size(btn_origin, 400, 100);
    lv_obj_align(btn_origin, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_bg_color(btn_origin, lv_color_hex(0x6EC4FF), 0); // Origin Ice
    lv_obj_add_event_cb(btn_origin, btn_origin_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_origin = lv_label_create(btn_origin);
    lv_label_set_text(lbl_origin, "ORIGIN LUX");
    lv_obj_set_style_text_color(lbl_origin, lv_color_hex(0x04070D), 0);
    lv_obj_set_style_text_font(lbl_origin, SC_FONT_MEDIUM, 0);
    lv_obj_align(lbl_origin, LV_ALIGN_CENTER, 0, 0);

    // Back Button
    lv_obj_t *btn_back = lv_button_create(scr);
    lv_obj_set_size(btn_back, 400, 80);
    lv_obj_align(btn_back, LV_ALIGN_CENTER, 0, 200);
    lv_obj_set_style_bg_color(btn_back, SC_COL_BG_PANEL, 0);
    lv_obj_add_event_cb(btn_back, btn_back_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK TO SETTINGS");
    lv_obj_set_style_text_color(lbl_back, SC_COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_back, SC_FONT_MEDIUM, 0);
    lv_obj_align(lbl_back, LV_ALIGN_CENTER, 0, 0);

    lv_unlock();
    return scr;
}
