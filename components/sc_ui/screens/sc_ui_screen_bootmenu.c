#include "sc_ui_screen_bootmenu.h"
#include "sc_ui_theme.h"
#include "sc_ui.h"
#include "sc_config.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "sc_ui_bootmenu";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_timer_label = NULL;
static lv_timer_t *s_countdown_timer = NULL;
static int s_time_left = 10;

static void continue_boot(void)
{
    if (s_countdown_timer) {
        lv_timer_delete(s_countdown_timer);
        s_countdown_timer = NULL;
    }

    sc_terminal_config_t *cfg = (sc_terminal_config_t *)sc_config_get();
    if (cfg->bridge_host[0] == '\0' || cfg->ship_id[0] == '\0') {
        sc_ui_router_push(SC_UI_SCREEN_PAIRING);
    } else {
        sc_ui_router_home();
    }
}

static void countdown_cb(lv_timer_t *timer)
{
    s_time_left--;
    if (s_time_left <= 0) {
        continue_boot();
    } else {
        if (s_timer_label) {
            lv_label_set_text_fmt(s_timer_label, "Booting in %ds...", s_time_left);
        }
    }
}

static void calibrate_btn_cb(lv_event_t *e)
{
    if (s_countdown_timer) {
        lv_timer_delete(s_countdown_timer);
        s_countdown_timer = NULL;
    }
    sc_ui_router_push(SC_UI_SCREEN_CALIBRATION);
}

static void boot_btn_cb(lv_event_t *e)
{
    continue_boot();
}

lv_obj_t *sc_ui_screen_bootmenu_create(void *user_data)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, SC_COL_BG, 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "reTerminal Touch Config");
    lv_obj_set_style_text_color(title, SC_COL_ACCENT, 0);
    lv_obj_set_style_text_font(title, SC_FONT_TITLE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SC_UI_PAD_LARGE);

    s_time_left = 10;
    s_timer_label = lv_label_create(s_scr);
    lv_label_set_text_fmt(s_timer_label, "Booting in %ds...", s_time_left);
    lv_obj_set_style_text_color(s_timer_label, SC_COL_TEXT, 0);
    lv_obj_set_style_text_font(s_timer_label, SC_FONT_MEDIUM, 0);
    lv_obj_align(s_timer_label, LV_ALIGN_TOP_MID, 0, SC_UI_PAD_LARGE + 60);

    lv_obj_t *cal_btn = lv_button_create(s_scr);
    lv_obj_set_size(cal_btn, 300, 80);
    lv_obj_set_style_bg_color(cal_btn, SC_COL_BG_PANEL, 0);
    lv_obj_set_style_bg_color(cal_btn, SC_COL_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_radius(cal_btn, SC_UI_BTN_RADIUS, 0);
    lv_obj_align(cal_btn, LV_ALIGN_CENTER, 0, -50);
    lv_obj_add_event_cb(cal_btn, calibrate_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cal_lbl = lv_label_create(cal_btn);
    lv_label_set_text(cal_lbl, "Calibrate Touch");
    lv_obj_set_style_text_color(cal_lbl, SC_COL_TEXT, 0);
    lv_obj_set_style_text_font(cal_lbl, SC_FONT_MEDIUM, 0);
    lv_obj_align(cal_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *boot_btn = lv_button_create(s_scr);
    lv_obj_set_size(boot_btn, 300, 80);
    lv_obj_set_style_bg_color(boot_btn, SC_COL_BG_PANEL, 0);
    lv_obj_set_style_bg_color(boot_btn, SC_COL_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_radius(boot_btn, SC_UI_BTN_RADIUS, 0);
    lv_obj_align(boot_btn, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_event_cb(boot_btn, boot_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *boot_lbl = lv_label_create(boot_btn);
    lv_label_set_text(boot_lbl, "Continue Boot");
    lv_obj_set_style_text_color(boot_lbl, SC_COL_TEXT, 0);
    lv_obj_set_style_text_font(boot_lbl, SC_FONT_MEDIUM, 0);
    lv_obj_align(boot_lbl, LV_ALIGN_CENTER, 0, 0);

    s_countdown_timer = lv_timer_create(countdown_cb, 1000, NULL);

    return s_scr;
}
