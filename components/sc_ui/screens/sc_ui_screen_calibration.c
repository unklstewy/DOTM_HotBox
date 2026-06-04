#include "sc_ui_screen_calibration.h"
#include "sc_ui_theme.h"
#include "sc_ui.h"
#include "sc_config.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "sc_ui_cal";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_crosshair = NULL;
static lv_obj_t *s_label = NULL;

static int s_step = 0;
static lv_point_t s_pts[3];

static const lv_point_t s_expected[3] = {
    {50, 50},
    {750, 50},
    {750, 1230}
};

static void update_crosshair(void)
{
    lv_obj_set_pos(s_crosshair, s_expected[s_step].x - 20, s_expected[s_step].y - 20);
    lv_label_set_text_fmt(s_label, "Calibration Step %d / 3\nTap the crosshair carefully.", s_step + 1);
}

static void compute_and_save_calibration(void)
{
    sc_terminal_config_t cfg = *sc_config_get();
    
    int32_t p1x = s_pts[0].x, p1y = s_pts[0].y;
    int32_t p2x = s_pts[1].x, p2y = s_pts[1].y;
    int32_t p3x = s_pts[2].x, p3y = s_pts[2].y;

    // Detect axis swap: P1->P2 is a horizontal move (50,50 -> 750,50).
    // If dy > dx, axes are swapped.
    bool swap_xy = abs(p2y - p1y) > abs(p2x - p1x);
    if (swap_xy) {
        int32_t tmp;
        tmp = p1x; p1x = p1y; p1y = tmp;
        tmp = p2x; p2x = p2y; p2y = tmp;
        tmp = p3x; p3x = p3y; p3y = tmp;
    }

    // Detect inversion
    bool invert_x = p2x < p1x;
    bool invert_y = p3y < p2y;

    // Calculate raw extents
    float scale_x = 700.0f / (float)abs(p2x - p1x);
    float scale_y = 1180.0f / (float)abs(p3y - p2y);

    int32_t raw_w = (int32_t)(800.0f / scale_x);
    int32_t raw_h = (int32_t)(1280.0f / scale_y);

    int32_t x_min, x_max, y_min, y_max;

    if (!invert_x) {
        x_min = p1x - (int32_t)(50.0f / scale_x);
    } else {
        x_min = p1x + (int32_t)(50.0f / scale_x);
    }
    x_max = x_min + (invert_x ? -raw_w : raw_w);

    // Make sure min < max
    if (x_min > x_max) { int32_t t = x_min; x_min = x_max; x_max = t; }

    if (!invert_y) {
        y_min = p2y - (int32_t)(50.0f / scale_y);
    } else {
        y_min = p2y + (int32_t)(50.0f / scale_y);
    }
    y_max = y_min + (invert_y ? -raw_h : raw_h);

    if (y_min > y_max) { int32_t t = y_min; y_min = y_max; y_max = t; }

    cfg.touch_cal.is_calibrated = true;
    cfg.touch_cal.swap_xy = swap_xy;
    cfg.touch_cal.invert_x = invert_x;
    cfg.touch_cal.invert_y = invert_y;
    cfg.touch_cal.x_min = x_min;
    cfg.touch_cal.x_max = x_max;
    cfg.touch_cal.y_min = y_min;
    cfg.touch_cal.y_max = y_max;

    ESP_LOGI(TAG, "Calibration complete: swap=%d inv_x=%d inv_y=%d X=[%ld, %ld] Y=[%ld, %ld]",
             swap_xy, invert_x, invert_y, (long)x_min, (long)x_max, (long)y_min, (long)y_max);

    sc_config_save(&cfg);
}

static void screen_click_event_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    ESP_LOGI(TAG, "Calibration tap %d at raw=(%d, %d)", s_step + 1, (int)pt.x, (int)pt.y);

    s_pts[s_step] = pt;
    s_step++;

    if (s_step < 3) {
        update_crosshair();
    } else {
        lv_label_set_text(s_label, "Calibration Complete!\nRebooting UI...");
        lv_obj_add_flag(s_crosshair, LV_OBJ_FLAG_HIDDEN);
        compute_and_save_calibration();
        
        // Return to home or pairing screen
        sc_terminal_config_t *cfg = (sc_terminal_config_t *)sc_config_get();
        if (cfg->bridge_host[0] == '\0' || cfg->ship_id[0] == '\0') {
            sc_ui_router_push(SC_UI_SCREEN_PAIRING);
        } else {
            sc_ui_router_home();
        }
    }
}

lv_obj_t *sc_ui_screen_calibration_create(void *user_data)
{
    lv_display_set_rotation(NULL, LV_DISPLAY_ROTATION_0);

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Must capture all clicks anywhere on the screen */
    lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_scr, screen_click_event_cb, LV_EVENT_CLICKED, NULL);

    s_label = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_label, SC_COL_TEXT, 0);
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_label, LV_ALIGN_CENTER, 0, 0);

    /* Draw crosshair (two lines inside a box) */
    s_crosshair = lv_obj_create(s_scr);
    lv_obj_set_size(s_crosshair, 40, 40);
    lv_obj_set_style_bg_opa(s_crosshair, 0, 0);
    lv_obj_set_style_border_color(s_crosshair, SC_COL_ACCENT, 0);
    lv_obj_set_style_border_width(s_crosshair, 2, 0);
    lv_obj_set_style_radius(s_crosshair, LV_RADIUS_CIRCLE, 0);
    
    // Add lines for crosshair
    static lv_point_precise_t h_line_pts[] = {{0, 20}, {40, 20}};
    lv_obj_t *h_line = lv_line_create(s_crosshair);
    lv_line_set_points(h_line, h_line_pts, 2);
    lv_obj_set_style_line_color(h_line, SC_COL_ACCENT, 0);
    lv_obj_set_style_line_width(h_line, 2, 0);
    
    static lv_point_precise_t v_line_pts[] = {{20, 0}, {20, 40}};
    lv_obj_t *v_line = lv_line_create(s_crosshair);
    lv_line_set_points(v_line, v_line_pts, 2);
    lv_obj_set_style_line_color(v_line, SC_COL_ACCENT, 0);
    lv_obj_set_style_line_width(v_line, 2, 0);

    // Disable clicking on the crosshair so clicks pass to the screen background
    lv_obj_remove_flag(s_crosshair, LV_OBJ_FLAG_CLICKABLE);

    s_step = 0;
    update_crosshair();

    return s_scr;
}
