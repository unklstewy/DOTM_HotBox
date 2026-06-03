#include "sc_ui_screen_origin.h"
#include "sc_ui_theme.h"
#include "sc_ui_screens.h"
#include "esp_log.h"

static const char *TAG = "sc_ui_origin";



static void back_event_handler(lv_event_t * e)
{
    sc_ui_router_pop();
}

lv_obj_t *sc_ui_screen_origin_create(lv_obj_t *parent) {
    lv_lock();
    sc_ui_theme_init_origin_lux();
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x04070D), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_obj_t *img = lv_image_create(scr);
    /* lv_image_set_src(img, &img_dsc_origin); */
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    // Invisible touch zone to go back
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 120, 120);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(btn, back_event_handler, LV_EVENT_CLICKED, NULL);

    lv_unlock();
    return scr;
}
