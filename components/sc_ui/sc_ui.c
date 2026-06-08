/*
 * sc_ui.c — LVGL display engine, touch driver, and screen router
 *
 * Owns the MIPI-DSI panel (800×1280), GT911 touch controller (I²C),
 * LVGL 9 task/tick, and the screen navigation stack.
 *
 * Frame buffers are allocated in PSRAM (MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA).
 * All LVGL calls from outside this task use lv_lock() / lv_unlock().
 */

#include "sc_ui.h"
#include "sc_ui_theme.h"
#include "sc_ui_screens.h"
#include "sc_ui_sprites.h"
#include "sc_ui_screen_console.h"
#include "sc_ui_screen_settings.h"
#include "sc_ui_screen_pairing.h"
#include "sc_ui_screen_calibration.h"
#include "sc_ui_screen_bootmenu.h"

#include "lvgl.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "sc_bsp.h"


static const char *TAG = "sc_ui";

/* ── LVGL task ────────────────────────────────────────────────────────────── */
#define SC_UI_LVGL_TICK_MS      (2)

/* Task stack lives in PSRAM (200 MHz — fast enough for LVGL's stack frames).
 * This frees 8 KB+ of internal SRAM for FreeRTOS kernel / ISR use. */
static TaskHandle_t  s_ui_task_handle = NULL;

/* ── Display & touch handles ─────────────────────────────────────────────── */
static lv_display_t                *s_lv_disp  = NULL;
static lv_indev_t                  *s_lv_indev = NULL;
static esp_timer_handle_t           s_lvgl_tick_timer = NULL;

/* ── Screen router stack ─────────────────────────────────────────────────── */
#define SC_UI_SCREEN_STACK_DEPTH (8)
static sc_ui_screen_id_t s_screen_stack[SC_UI_SCREEN_STACK_DEPTH];
static int               s_screen_sp = -1;   /* stack pointer */
static const sc_terminal_config_t *s_cfg = NULL;
static lv_obj_t                 *s_battery_label = NULL;
static esp_timer_handle_t        s_ui_timer = NULL;

/* ── Forward declarations ────────────────────────────────────────────────── */
static void sc_ui_lvgl_task(void *arg);
static void sc_ui_lvgl_tick_cb(void *arg);
static void sc_ui_periodic_cb(void *arg);
static lv_obj_t *sc_ui_screen_create(sc_ui_screen_id_t id);
static esp_err_t sc_ui_screen_show(sc_ui_screen_id_t id);

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

static void sc_ui_bsp_triple_click_cb(void)
{
    ESP_LOGI(TAG, "BSP triple click callback triggered. Navigating to touch calibration.");
    sc_ui_router_push(SC_UI_SCREEN_CALIBRATION);
}

esp_err_t sc_ui_init(const sc_terminal_config_t *cfg)
{
    s_cfg = cfg;
    ESP_LOGI(TAG, "UI init start");

    sc_ui_theme_init_default();

    ESP_LOGI(TAG, "Init BSP");
    esp_err_t ret = sc_bsp_init();
    if (ret != ESP_OK) return ret;

    sc_bsp_register_btn_triple_click_cb(sc_ui_bsp_triple_click_cb);

    /* LVGL init */
    lv_init();

    s_lv_disp = sc_bsp_display_create();
    if (s_lv_disp) {
        lv_display_rotation_t rot = LV_DISPLAY_ROTATION_0;
        if (cfg->display_rotation == 1) rot = LV_DISPLAY_ROTATION_90;
        else if (cfg->display_rotation == 2) rot = LV_DISPLAY_ROTATION_180;
        else if (cfg->display_rotation == 3) rot = LV_DISPLAY_ROTATION_270;
        lv_display_set_rotation(s_lv_disp, rot);
    }
    s_lv_indev = sc_bsp_touch_create();

    const esp_timer_create_args_t tick_args = {
        .callback = sc_ui_lvgl_tick_cb,
        .name = "sc_ui_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &s_lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvgl_tick_timer, SC_UI_LVGL_TICK_MS * 1000));
    
    /* UI periodic updates (e.g. battery) */
    const esp_timer_create_args_t ui_timer_args = {
        .callback = sc_ui_periodic_cb,
        .name = "sc_ui_periodic",
    };
    ESP_ERROR_CHECK(esp_timer_create(&ui_timer_args, &s_ui_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_ui_timer, 5000000)); /* 5 seconds */

    /* Create global battery indicator */
    s_battery_label = lv_label_create(lv_layer_top());
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_label_set_text(s_battery_label, "BATT --%");
    lv_obj_set_style_text_color(s_battery_label, lv_color_hex(0x808080), 0);
    
    ESP_LOGI(TAG, "LVGL tick timer started");

    /* Start LVGL task — stack in internal memory to allow flash writes (like config saving)
     * during UI execution, since SPI flash operations temporarily disable caches. */
    BaseType_t rc = xTaskCreateWithCaps(
        sc_ui_lvgl_task,
        "sc_ui_lvgl",
        SC_UI_TASK_STACK_SIZE,
        NULL,
        SC_UI_TASK_PRIORITY,
        &s_ui_task_handle,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    if (rc != pdPASS) return ESP_FAIL;
    ESP_LOGI(TAG, "LVGL task started");

    /* Navigate to initial screen */
    if (!cfg->touch_cal.is_calibrated) {
        sc_ui_router_push(SC_UI_SCREEN_CALIBRATION);
    } else {
        sc_ui_router_push(SC_UI_SCREEN_SPLASH);
    }

    ESP_LOGI(TAG, "UI subsystem started (%dx%d)",
             SC_UI_DISPLAY_WIDTH, SC_UI_DISPLAY_HEIGHT);
    return ESP_OK;
}

void sc_ui_deinit(void)
{
    if (s_ui_task_handle) {
        vTaskDelete(s_ui_task_handle);
        s_ui_task_handle = NULL;
    }
    if (s_lvgl_tick_timer) {
        esp_timer_stop(s_lvgl_tick_timer);
        esp_timer_delete(s_lvgl_tick_timer);
        s_lvgl_tick_timer = NULL;
    }
    if (s_ui_timer) {
        esp_timer_stop(s_ui_timer);
        esp_timer_delete(s_ui_timer);
        s_ui_timer = NULL;
    }
    sc_ui_sprites_unload();   /* free PSRAM atlas before LVGL teardown */
    lv_deinit();
}

/* ── Screen Router ───────────────────────────────────────────────────────── */

esp_err_t sc_ui_router_push(sc_ui_screen_id_t id)
{
    if (s_screen_sp >= SC_UI_SCREEN_STACK_DEPTH - 1) return ESP_ERR_NO_MEM;
    s_screen_stack[++s_screen_sp] = id;
    return sc_ui_screen_show(id);
}

esp_err_t sc_ui_router_pop(void)
{
    if (s_screen_sp <= 0) return ESP_ERR_INVALID_STATE;
    s_screen_sp--;
    return sc_ui_screen_show(s_screen_stack[s_screen_sp]);
}

esp_err_t sc_ui_router_home(void)
{
    s_screen_sp = 0;
    s_screen_stack[0] = SC_UI_SCREEN_CONSOLE;
    return sc_ui_screen_show(SC_UI_SCREEN_CONSOLE);
}

esp_err_t sc_ui_brightness_set(uint8_t percent)
{
    return sc_bsp_brightness_set(percent);
}

/* ── Screen factory ──────────────────────────────────────────────────────── */

static lv_obj_t *sc_ui_screen_create(sc_ui_screen_id_t id)
{
    switch (id) {
        case SC_UI_SCREEN_CONSOLE:
            {
                sc_ui_screen_console_load(s_cfg);
                lv_obj_t *scr = sc_ui_screen_console_create(NULL);
                return scr;
            }
        case SC_UI_SCREEN_SETTINGS:
            return sc_ui_screen_settings_create(NULL);
        case SC_UI_SCREEN_PAIRING:
            return sc_ui_screen_pairing_create(NULL);
        case SC_UI_SCREEN_DRAKE:
            return sc_ui_screen_drake_create(NULL);
        case SC_UI_SCREEN_ORIGIN:
            return sc_ui_screen_origin_create(NULL);
        case SC_UI_SCREEN_THEME_SELECTOR:
            return sc_ui_screen_theme_selector_create(NULL);
        case SC_UI_SCREEN_CALIBRATION:
            return sc_ui_screen_calibration_create(NULL);
        case SC_UI_SCREEN_BOOTMENU:
            return sc_ui_screen_bootmenu_create(NULL);
        case SC_UI_SCREEN_SPLASH:
            return sc_ui_screen_splash_create(NULL);
        default:
            ESP_LOGW(TAG, "Unknown screen id: %d", id);
            return NULL;
    }
}

static esp_err_t sc_ui_screen_show(sc_ui_screen_id_t id)
{
    lv_lock();
    lv_obj_t *scr = sc_ui_screen_create(id);
    if (!scr) {
        lv_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Load screen id=%d", id);
    lv_screen_load(scr);
    lv_unlock();
    return ESP_OK;
}

/* ── LVGL task ───────────────────────────────────────────────────────────── */

static void sc_ui_lvgl_task(void *arg)
{
    (void)arg;
    for (;;) {
        lv_lock();
        uint32_t delay_ms = lv_timer_handler();
        lv_unlock();
        if (delay_ms == LV_NO_TIMER_READY || delay_ms > 30) delay_ms = 30;
        if (delay_ms < SC_UI_LVGL_TICK_MS)                  delay_ms = SC_UI_LVGL_TICK_MS;
        
        uint32_t ticks = pdMS_TO_TICKS(delay_ms);
        if (ticks == 0) ticks = 1; /* prevent starving the IDLE task and triggering task watchdog */
        vTaskDelay(ticks);
    }
}

static void sc_ui_lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(SC_UI_LVGL_TICK_MS);
}

static void sc_ui_periodic_cb(void *arg)
{
    (void)arg;
    if (s_battery_label) {
        int pct = sc_bsp_get_battery_pct();
        lv_lock();
        lv_label_set_text_fmt(s_battery_label, "BATT %d%%", pct);
        lv_unlock();
    }
}

void sc_ui_set_rotation(uint8_t rotation)
{
    lv_lock();
    lv_display_t *disp = s_lv_disp;
    if (!disp) disp = lv_display_get_default();
    if (disp) {
        lv_display_rotation_t rot = LV_DISPLAY_ROTATION_0;
        if (rotation == 1) rot = LV_DISPLAY_ROTATION_90;
        else if (rotation == 2) rot = LV_DISPLAY_ROTATION_180;
        else if (rotation == 3) rot = LV_DISPLAY_ROTATION_270;
        lv_display_set_rotation(disp, rot);
    }
    lv_unlock();
}


