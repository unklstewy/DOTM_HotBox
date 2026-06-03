#include "sc_ui_screen_drake.h"
#include "sc_ui_theme.h"
#include "sc_ui_screens.h"
#include "esp_log.h"

static const char *TAG = "sc_ui_drake";

extern const uint8_t _binary_mockup_drake_bin_start[];
extern const uint8_t _binary_mockup_drake_bin_end[];

static lv_image_dsc_t img_dsc_drake = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = 800,
        .h = 1280,
        .stride = 800 * 2,
    },
    .data_size = 800 * 1280 * 2,
    .data = _binary_mockup_drake_bin_start,
};

static void back_event_handler(lv_event_t * e)
{
    sc_ui_router_pop();
}

lv_obj_t *sc_ui_screen_drake_create(lv_obj_t *parent) {
    lv_lock();
    sc_ui_theme_init_drake_military();
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &img_dsc_drake);
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
