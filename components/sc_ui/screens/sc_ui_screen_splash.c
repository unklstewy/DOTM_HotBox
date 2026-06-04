#include "sc_ui_screen_splash.h"
#include "sc_ui_theme.h"
#include "sc_ui.h"
#include "sc_config.h"
#include "sc_ui_sprites.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>

static const char *TAG = "sc_ui_splash";

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_progress_bar = NULL;
static lv_obj_t *s_status_lbl = NULL;

static void splash_progress_cb(int pct)
{
    lv_lock();
    if (s_progress_bar) {
        lv_bar_set_value(s_progress_bar, pct, LV_ANIM_ON);
    }
    if (s_status_lbl) {
        lv_label_set_text_fmt(s_status_lbl, "Rasterizing sprites... %d%%", pct);
    }
    lv_unlock();
}

static void rasterize_thread_func(void *arg)
{
    const sc_terminal_config_t *cfg = sc_config_get();
    ESP_LOGI(TAG, "Loading sprites for ship: %s", cfg->ship_id);

    /* Load pre-rasterized sprite .bin files from the SD card.
     * These are produced on the host by tools/rasterize_sprites.py and
     * deployed by tools/prep_sdcard.sh.  Pure file I/O — no LVGL rendering,
     * no lv_lock(), no draw-thread interaction, no WDT issues. */
    esp_err_t ret = sc_ui_sprites_load_from_sdcard(
        "/sdcard/assets/themes/drake",
        splash_progress_cb
    );
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Sprite load failed (%s) — UI will use flat-colour fallback",
                 esp_err_to_name(ret));
    }

    vTaskDelay(pdMS_TO_TICKS(300));

    lv_lock();
    ESP_LOGI(TAG, "Rasterization complete, routing to next boot screen");
#if CONFIG_SC_BRIDGE_ENABLED
    // With bridge: go to pairing if no host is configured
    if (cfg->bridge_host[0] == '\0' || cfg->ship_id[0] == '\0') {
        sc_ui_router_push(SC_UI_SCREEN_PAIRING);
    } else {
        sc_ui_router_push(SC_UI_SCREEN_BOOTMENU);
    }
#else
    // Without bridge: always go straight to bootmenu
    sc_ui_router_push(SC_UI_SCREEN_BOOTMENU);
#endif
    lv_unlock();

    vTaskDelete(NULL);
}

lv_obj_t *sc_ui_screen_splash_create(void *user_data)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000), 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    // Full screen background image
    lv_obj_t *bg_img = lv_image_create(s_scr);
    lv_image_set_src(bg_img, "S:/assets/images/splash_base_portait.png");
    lv_obj_set_size(bg_img, LV_PCT(100), LV_PCT(100));
    lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0);

    // Centered branding logo
    lv_obj_t *logo_img = lv_image_create(s_scr);
    lv_image_set_src(logo_img, "S:/assets/images/DanksideLogo.png");
    lv_obj_align(logo_img, LV_ALIGN_CENTER, 0, -150);

    // Progress Bar at the bottom
    s_progress_bar = lv_bar_create(s_scr);
    lv_obj_set_size(s_progress_bar, 500, 20);
    lv_obj_align(s_progress_bar, LV_ALIGN_CENTER, 0, 200);
    lv_bar_set_range(s_progress_bar, 0, 100);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);

    // Customize progress bar style to match theme
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x1a1f29), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x6ec4ff), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_progress_bar, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress_bar, 10, LV_PART_INDICATOR);

    // Status / progress label
    s_status_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_status_lbl, "Initializing systems... 0%");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xe8eef6), 0);
    lv_obj_set_style_text_font(s_status_lbl, SC_FONT_MEDIUM, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 0, 240);

    // Start background rasterization task
    // 12 KB stack: ThorVG SVG renderer is deeply recursive and stack-hungry.
    xTaskCreate(rasterize_thread_func, "sc_splash_rast", 12288, NULL, 3, NULL);

    return s_scr;
}
