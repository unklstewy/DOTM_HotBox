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
#include "sc_ui_screen_console.h"
#include "sc_ui_screen_settings.h"
#include "sc_ui_screen_pairing.h"
#include "sc_ui_screen_calibration.h"
#include "sc_ui_screen_bootmenu.h"

#include "lvgl.h"
#include "esp_idf_version.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_jd9365_8.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gsl3670.h"
#include "esp_io_expander.h"
#include "esp_io_expander_pca9535.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "sc_ui";

/* ── Pin definitions (reTerminal D1001, 8" 800×1280 JD9365 panel) ──────── */
/* Backlight PWM is wired to GPIO 14. (The IO-expander pin BSP_LCD_BACKLIGHT_EN
 * is the backlight power-rail gate; this PWM pin sets brightness on top.)    */
#define SC_UI_BL_GPIO          (GPIO_NUM_14)

/* PCA9535 IO expander on I2C bus 1 — controls panel power, backlight power,
 * LCD reset, and the 3V3 rail PWR_HOLD which everything depends on.          */
#define SC_UI_EXP_I2C_PORT     (I2C_NUM_1)
#define SC_UI_EXP_I2C_SCL      (GPIO_NUM_21)
#define SC_UI_EXP_I2C_SDA      (GPIO_NUM_20)
#define SC_UI_EXP_I2C_FREQ     (400000)
#define SC_UI_EXP_PWR_HOLD     (1ULL << 8)   /* vdd_3v3 enable */
#define SC_UI_EXP_LCD_PWR_EN   (1ULL << 0)   /* panel VCC enable */
#define SC_UI_EXP_LCD_BL_EN    (1ULL << 7)   /* backlight rail enable */
#define SC_UI_EXP_LCD_RST      (1ULL << 2)   /* panel HW reset (active low) */
#define SC_UI_EXP_TOUCH_RST    (1ULL << 12)  /* touch HW reset */

/* Touch controller — GT911 over I2C bus 0 */
#define SC_UI_TOUCH_I2C_PORT   (I2C_NUM_0)
#define SC_UI_TOUCH_SCL        (GPIO_NUM_38)
#define SC_UI_TOUCH_SDA        (GPIO_NUM_37)
#define SC_UI_TOUCH_RST        (GPIO_NUM_NC)   /* on IO expander */
#define SC_UI_TOUCH_INT        (GPIO_NUM_16)
#define SC_UI_TOUCH_I2C_FREQ   (400000)

/* Power and Battery */
#define SC_UI_PWR_BUTTON       (GPIO_NUM_3)
#define SC_UI_PWR_IN_VOLT      (GPIO_NUM_15)
#define SC_UI_EN_READ_VBAT     (GPIO_NUM_6)

#define SC_UI_DSI_PHY_LDO_CHAN       (3)
#define SC_UI_DSI_PHY_LDO_MV         (2500)
#define SC_UI_DPI_CLOCK_FREQ_MHZ     (60)
#define SC_UI_DSI_LANE_BITRATE_MBPS  (1000)   /* Seeed D1001 BSP value */
#define SC_UI_DRAW_BUF_LINES         (80)
#define SC_UI_ENABLE_TOUCH           (1)
#define SC_UI_DBG_FLUSH_LOG_INITIAL  (8)
#define SC_UI_DBG_FLUSH_LOG_EVERY    (120)
#define SC_UI_DBG_TOUCH_LOG_MS       (250)

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
#define SC_UI_DPI_COLOR_FORMAT LCD_COLOR_PIXEL_FORMAT_RGB565
#else
#define SC_UI_DPI_COLOR_FORMAT LCD_COLOR_FMT_RGB565
#endif

/* ── LVGL task ────────────────────────────────────────────────────────────── */
#define SC_UI_LVGL_TICK_MS      (2)

static StaticTask_t  s_ui_task_buf;
static StackType_t   s_ui_task_stack[SC_UI_TASK_STACK_SIZE];
static TaskHandle_t  s_ui_task_handle = NULL;

/* ── Display & touch handles ─────────────────────────────────────────────── */
static esp_ldo_channel_handle_t     s_dsi_ldo  = NULL;
static esp_lcd_dsi_bus_handle_t     s_dsi_bus  = NULL;
static esp_lcd_panel_io_handle_t    s_panel_io = NULL;
static esp_lcd_panel_handle_t       s_panel    = NULL;
static esp_lcd_touch_handle_t       s_touch    = NULL;
static lv_display_t                *s_lv_disp  = NULL;
static esp_timer_handle_t           s_lvgl_tick_timer = NULL;
static i2c_master_bus_handle_t      s_exp_i2c_bus = NULL;
static esp_io_expander_handle_t     s_io_expander = NULL;
esp_io_expander_handle_t io_expander = NULL; /* Exported for GSL3670 driver */
static uint8_t                     *s_fb[2]    = {NULL, NULL};
static uint32_t                     s_flush_seq = 0;
static int64_t                      s_touch_last_log_us = 0;
static bool                         s_touch_last_pressed = false;

/* ── Screen router stack ─────────────────────────────────────────────────── */
#define SC_UI_SCREEN_STACK_DEPTH (8)
static sc_ui_screen_id_t s_screen_stack[SC_UI_SCREEN_STACK_DEPTH];
static int               s_screen_sp = -1;   /* stack pointer */
static const sc_terminal_config_t *s_cfg = NULL;

/* ── Power & Battery State ───────────────────────────────────────────────── */
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_adc_cali = NULL;
static adc_channel_t             s_adc_chan = 0;
static esp_timer_handle_t        s_pwr_timer = NULL;
static uint32_t                  s_btn_press_ms = 0;
static uint32_t                  s_last_release_ms = 0;
static int                       s_battery_pct = 100;
static lv_obj_t                 *s_battery_label = NULL;

/* ── Forward declarations ────────────────────────────────────────────────── */
static void sc_ui_lvgl_task(void *arg);
static void sc_ui_lvgl_tick_cb(void *arg);
static void sc_ui_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                                uint8_t *px_map);
static void sc_ui_lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data);
static esp_err_t sc_ui_io_expander_init(void);
static esp_err_t sc_ui_display_init(void);
static esp_err_t sc_ui_touch_init(void);
static esp_err_t sc_ui_backlight_init(void);
static void sc_ui_power_init(void);
static void sc_ui_pwr_timer_cb(void *arg);
static lv_obj_t *sc_ui_screen_create(sc_ui_screen_id_t id);
static esp_err_t sc_ui_screen_show(sc_ui_screen_id_t id);

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

esp_err_t sc_ui_init(const sc_terminal_config_t *cfg)
{
    s_cfg = cfg;
    ESP_LOGI(TAG, "UI init start");

    sc_ui_power_init();
    sc_ui_theme_init_default();

    ESP_LOGI(TAG, "Init PCA9535 IO expander (LCD power/reset/backlight gates)");
    esp_err_t ret = sc_ui_io_expander_init();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Init backlight");
    ret = sc_ui_backlight_init();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Init display");
    ret = sc_ui_display_init();
    if (ret != ESP_OK) return ret;

    if (SC_UI_ENABLE_TOUCH) {
        ESP_LOGI(TAG, "Init touch");
        ret = sc_ui_touch_init();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Touch init failed, continuing without touch: %s",
                     esp_err_to_name(ret));
            s_touch = NULL;
        }
    } else {
        s_touch = NULL;
        ESP_LOGW(TAG, "Touch init is disabled (SC_UI_ENABLE_TOUCH=0)");
    }

    /* The hardware still needs its framebuffer to scan out from */
    void *panel_fb0 = NULL;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &panel_fb0));
    ESP_LOGI(TAG, "Panel framebuffer at %p", panel_fb0);
    s_fb[0] = panel_fb0;
    s_fb[1] = NULL;

    /* LVGL init */
    lv_init();

    s_lv_disp = lv_display_create(SC_UI_DISPLAY_WIDTH, SC_UI_DISPLAY_HEIGHT);
    lv_display_set_color_format(s_lv_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(s_lv_disp, s_panel);
    lv_display_set_flush_cb(s_lv_disp, sc_ui_lvgl_flush_cb);

    /* Allocate a dedicated LVGL draw buffer (1/10 screen size) in internal SRAM to 
     * completely eliminate PSRAM bandwidth congestion (which causes DPI underruns).
     * The DPI driver will use DMA2D to copy this buffer into the active panel_fb0 during flush. */
    uint32_t draw_buf_size = SC_UI_DISPLAY_WIDTH * SC_UI_DISPLAY_HEIGHT * 2 / 10;
    void *draw_buf = heap_caps_malloc(draw_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!draw_buf) {
        ESP_LOGW(TAG, "SRAM too small for draw buffer, falling back to PSRAM!");
        draw_buf = heap_caps_malloc(draw_buf_size, MALLOC_CAP_SPIRAM);
    }
    lv_display_set_buffers(s_lv_disp, draw_buf, NULL, draw_buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    ESP_LOGI(TAG, "LVGL display configured (partial render, 1/10 screen draw buffer)");

    if (s_touch) {
        lv_indev_t *touch_indev = lv_indev_create();
        lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(touch_indev, sc_ui_lvgl_touch_cb);
    }

    const esp_timer_create_args_t tick_args = {
        .callback = sc_ui_lvgl_tick_cb,
        .name = "sc_ui_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &s_lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvgl_tick_timer,
                                             SC_UI_LVGL_TICK_MS * 1000));
    
    /* Create global battery indicator */
    s_battery_label = lv_label_create(lv_layer_top());
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_label_set_text(s_battery_label, "BATT --%");
    lv_obj_set_style_text_color(s_battery_label, lv_color_hex(0x808080), 0);
    
    ESP_LOGI(TAG, "LVGL tick timer started");

    /* Start LVGL task */
    s_ui_task_handle = xTaskCreateStatic(
        sc_ui_lvgl_task,
        "sc_ui_lvgl",
        SC_UI_TASK_STACK_SIZE,
        NULL,
        SC_UI_TASK_PRIORITY,
        s_ui_task_stack,
        &s_ui_task_buf
    );
    if (!s_ui_task_handle) return ESP_FAIL;
    ESP_LOGI(TAG, "LVGL task started");

    /* Navigate to initial screen */
    if (!cfg->touch_cal.is_calibrated) {
        sc_ui_router_push(SC_UI_SCREEN_CALIBRATION);
    } else {
        sc_ui_router_push(SC_UI_SCREEN_BOOTMENU);
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
    lv_deinit();
    for (int i = 0; i < 2; i++) {
        if (s_fb[i]) { free(s_fb[i]); s_fb[i] = NULL; }
    }
    if (s_touch) {
        esp_lcd_touch_del(s_touch);
        s_touch = NULL;
    }
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_panel_io) {
        esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = NULL;
    }
    if (s_dsi_bus) {
        esp_lcd_del_dsi_bus(s_dsi_bus);
        s_dsi_bus = NULL;
    }
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
    if (percent > 100) percent = 100;
    uint32_t duty = (percent * ((1 << LEDC_TIMER_10_BIT) - 1)) / 100;
    return ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                    duty, 0);
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
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static void sc_ui_lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(SC_UI_LVGL_TICK_MS);
}

/* ── Power & Battery ─────────────────────────────────────────────────────── */

static void sc_ui_power_init(void)
{
    /* Power button config */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << SC_UI_PWR_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_cfg);

    /* Battery enable config */
    gpio_config_t en_cfg = {
        .pin_bit_mask = (1ULL << SC_UI_EN_READ_VBAT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&en_cfg);
    gpio_set_level(SC_UI_EN_READ_VBAT, 0);

    /* ADC config */
    adc_unit_t unit;
    if (adc_oneshot_io_to_channel(SC_UI_PWR_IN_VOLT, &unit, &s_adc_chan) == ESP_OK) {
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = unit,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc_handle));

        adc_oneshot_chan_cfg_t config = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = ADC_ATTEN_DB_12, /* 12dB attenuation for voltages > 3V */
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, s_adc_chan, &config));

        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        adc_cali_create_scheme_curve_fitting(&cali_config, &s_adc_cali);
    }

    const esp_timer_create_args_t pwr_timer_args = {
        .callback = &sc_ui_pwr_timer_cb,
        .name = "sc_ui_pwr"
    };
    ESP_ERROR_CHECK(esp_timer_create(&pwr_timer_args, &s_pwr_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_pwr_timer, 50000)); /* 50ms polling */
}

static void sc_ui_pwr_timer_cb(void *arg)
{
    static uint32_t s_ticks = 0;
    uint32_t now = esp_log_timestamp();
    int btn_state = gpio_get_level(SC_UI_PWR_BUTTON);

    /* Button logic (Active High) */
    if (btn_state == 1) {
        if (s_btn_press_ms == 0) {
            s_btn_press_ms = now;
        } else if (now - s_btn_press_ms > 2000) {
            /* Long press detected -> Power Off */
            ESP_LOGW(TAG, "Long press detected! Powering off.");
            if (s_io_expander) {
                esp_io_expander_set_level(s_io_expander, SC_UI_EXP_PWR_HOLD, 0);
            }
            /* Prevent log spam */
            s_btn_press_ms = now + 10000;
        }
    } else {
        if (s_btn_press_ms > 0 && s_btn_press_ms <= now) {
            uint32_t duration = now - s_btn_press_ms;
            s_btn_press_ms = 0;
            if (duration < 500 && duration > 20) {
                /* Short press */
                if (now - s_last_release_ms < 400) {
                    /* Double tap detected -> Reset */
                    ESP_LOGW(TAG, "Double tap detected! Restarting...");
                    esp_restart();
                } else {
                    s_last_release_ms = now;
                }
            }
        } else {
            s_btn_press_ms = 0;
        }
    }

    /* Battery reading every 5 seconds */
    s_ticks++;
    if (s_ticks % 100 == 0 && s_adc_handle) {
        gpio_set_level(SC_UI_EN_READ_VBAT, 1);
        
        /* The timer runs in a high priority task, we don't want to block too long, but we need the ADC to stabilize */
        int raw, mv = 0;
        if (adc_oneshot_read(s_adc_handle, s_adc_chan, &raw) == ESP_OK) {
            if (s_adc_cali) {
                adc_cali_raw_to_voltage(s_adc_cali, raw, &mv);
            }
        }
        gpio_set_level(SC_UI_EN_READ_VBAT, 0);

        if (mv > 0) {
            /* Assuming voltage divider (e.g., 2:1) */
            int batt_mv = mv * 2; /* Needs to be adjusted based on real divider */
            
            /* Rough Lipo curve: 3.2V (0%) to 4.2V (100%) */
            int pct = (batt_mv - 3200) / 10;
            if (pct > 100) pct = 100;
            if (pct < 0) pct = 0;
            s_battery_pct = pct;

            if (s_battery_label) {
                lv_lock();
                lv_label_set_text_fmt(s_battery_label, "BATT %d%%", s_battery_pct);
                lv_unlock();
            }
        }
    }
}

/* ── LVGL flush callback ─────────────────────────────────────────────────── */

static void sc_ui_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                                uint8_t *px_map)
{
    (void)area;
    (void)px_map;
    s_flush_seq++;
    if (s_flush_seq <= SC_UI_DBG_FLUSH_LOG_INITIAL ||
        (s_flush_seq % SC_UI_DBG_FLUSH_LOG_EVERY) == 0) {
        ESP_LOGI(TAG, "Flush #%lu area=(%d,%d)-(%d,%d)",
                 (unsigned long)s_flush_seq,
                 (int)area->x1, (int)area->y1,
                 (int)area->x2, (int)area->y2);
    }
    /* Zero-copy: LVGL drew straight into the panel framebuffer (in PSRAM).
     * The MIPI-DSI DMA reads PSRAM bypassing the CPU cache, so we still need
     * to push the dirty region through draw_bitmap to trigger a cache write-
     * back (the DPI driver does esp_cache_msync internally). */
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

/* ── Touch read callback ─────────────────────────────────────────────────── */

static void sc_ui_lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    if (!s_touch) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    esp_lcd_touch_point_data_t pt[1];
    uint8_t count = 0;
    esp_lcd_touch_read_data(s_touch);
    esp_err_t ret = esp_lcd_touch_get_data(s_touch, pt, &count, 1);
    bool pressed = (ret == ESP_OK) && (count > 0);

    if (ret != ESP_OK && !s_touch_last_pressed) {
        ESP_LOGW(TAG, "Touch read error: %s", esp_err_to_name(ret));
    }

    if (pressed && count > 0) {
        int32_t raw_x = (int32_t)pt[0].x;
        int32_t raw_y = (int32_t)pt[0].y;
        
        bool calibrating = (s_screen_sp >= 0 && s_screen_stack[s_screen_sp] == SC_UI_SCREEN_CALIBRATION);
        
        if (s_cfg && s_cfg->touch_cal.is_calibrated && !calibrating) {
            if (s_cfg->touch_cal.swap_xy) {
                int32_t tmp = raw_x; raw_x = raw_y; raw_y = tmp;
            }
            int32_t dx = s_cfg->touch_cal.x_max - s_cfg->touch_cal.x_min;
            int32_t dy = s_cfg->touch_cal.y_max - s_cfg->touch_cal.y_min;
            if (dx != 0 && dy != 0) {
                raw_x = (raw_x - s_cfg->touch_cal.x_min) * SC_UI_DISPLAY_WIDTH / dx;
                raw_y = (raw_y - s_cfg->touch_cal.y_min) * SC_UI_DISPLAY_HEIGHT / dy;
            }
            if (s_cfg->touch_cal.invert_x) raw_x = SC_UI_DISPLAY_WIDTH - 1 - raw_x;
            if (s_cfg->touch_cal.invert_y) raw_y = SC_UI_DISPLAY_HEIGHT - 1 - raw_y;
        }

        data->point.x = raw_x;
        data->point.y = raw_y;
        data->state   = LV_INDEV_STATE_PRESSED;

        if (!s_touch_last_pressed) {
            ESP_LOGI(TAG, "Touch press raw=(%u,%u) cal=(%ld,%ld) strength=%u track=%u",
                     (unsigned int)pt[0].x, (unsigned int)pt[0].y,
                     (long)raw_x, (long)raw_y,
                     (unsigned int)pt[0].strength,
                     (unsigned int)pt[0].track_id);
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        if (s_touch_last_pressed) {
            ESP_LOGI(TAG, "Touch release");
        }
    }

    s_touch_last_pressed = pressed;
}

/* ── Hardware init stubs ─────────────────────────────────────────────────── */

static esp_err_t sc_ui_backlight_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    const ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = SC_UI_BL_GPIO,
        .duty       = 512,   /* 50% at boot */
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
    return ESP_OK;
}

static esp_err_t sc_ui_io_expander_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port    = SC_UI_EXP_I2C_PORT,
        .sda_io_num  = SC_UI_EXP_I2C_SDA,
        .scl_io_num  = SC_UI_EXP_I2C_SCL,
        .clk_source  = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_exp_i2c_bus));

    ESP_ERROR_CHECK(esp_io_expander_new_i2c_pca9535(
        s_exp_i2c_bus,
        ESP_IO_EXPANDER_I2C_PCA9535_ADDRESS_000,
        &s_io_expander));

    io_expander = s_io_expander;

    ESP_ERROR_CHECK(esp_io_expander_set_dir(s_io_expander, 0xFFFF, IO_EXPANDER_OUTPUT));
    /* Bring up the 3V3 power-hold rail first; everything else needs it. */
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_PWR_HOLD,  1));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_LCD_PWR_EN, 1));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_LCD_BL_EN,  1));
    /* Hold LCD in reset until the panel driver is configured. */
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_LCD_RST,   1));
    
    /* Touch controller HW reset */
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_TOUCH_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_TOUCH_RST, 1));
    
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "PCA9535 ready: PWR_HOLD=LCD_PWR=BL_EN=1, LCD_RST released");
    return ESP_OK;
}

static esp_err_t sc_ui_display_init(void)
{
    /* MIPI-DSI PHY LDO power up first. */
    if (!s_dsi_ldo) {
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = SC_UI_DSI_PHY_LDO_CHAN,
            .voltage_mv = SC_UI_DSI_PHY_LDO_MV,
        };
        ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &s_dsi_ldo));
        ESP_LOGI(TAG, "MIPI DSI PHY powered from LDO channel %d",
                 SC_UI_DSI_PHY_LDO_CHAN);
    }

    /* DSI bus: 2 lanes @ 1000 Mbps (Seeed BSP value for 8" panel). */
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = SC_UI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_LOGI(TAG, "Initialize MIPI DSI bus (2 lanes, %u Mbps)",
             (unsigned)SC_UI_DSI_LANE_BITRATE_MBPS);
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &s_dsi_bus));

    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ESP_LOGI(TAG, "Install panel IO (DBI)");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_config, &s_panel_io));

    /* DPI panel timings — taken verbatim from Seeed's
     * JD9365_8_800_1280_PANEL_60HZ_DPI_CONFIG macro. */
    esp_lcd_dpi_panel_config_t dpi_config =
        JD9365_8_800_1280_PANEL_60HZ_DPI_CONFIG(SC_UI_DPI_COLOR_FORMAT);
    dpi_config.num_fbs = 1;

    jd9365_8_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus    = s_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num   = 2,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,                       /* reset is on IO expander */
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = (void *)&vendor_config,
    };

    ESP_LOGI(TAG, "Install JD9365 (8\") panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365_8(s_panel_io, &panel_config, &s_panel));

    /* Hardware reset pulse via PCA9535 expander, as Seeed BSP does. */
    ESP_LOGI(TAG, "Pulse LCD reset via IO expander");
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(120));

    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    ESP_LOGI(TAG, "Display init complete (%dx%d JD9365_8 over MIPI-DSI, dpi_clk=%uMHz)",
             SC_UI_DISPLAY_WIDTH, SC_UI_DISPLAY_HEIGHT,
             (unsigned)SC_UI_DPI_CLOCK_FREQ_MHZ);
    return ESP_OK;
}

static esp_err_t sc_ui_touch_init(void)
{
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port    = SC_UI_TOUCH_I2C_PORT,
        .sda_io_num  = SC_UI_TOUCH_SDA,
        .scl_io_num  = SC_UI_TOUCH_SCL,
        .clk_source  = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &bus));

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GSL3670_CONFIG();
    tp_io_cfg.scl_speed_hz = SC_UI_TOUCH_I2C_FREQ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, &tp_io));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max        = SC_UI_DISPLAY_WIDTH,
        .y_max        = SC_UI_DISPLAY_HEIGHT,
        .rst_gpio_num = 12, /* IO expander pin 12 */
        .int_gpio_num = SC_UI_TOUCH_INT,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy   = 0,
            .mirror_x  = 0,
            .mirror_y  = 0,
        },
    };
    
    esp_err_t ret = esp_lcd_touch_new_i2c_gsl3670(tp_io, &tp_cfg, &s_touch);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Touch init complete (GSL3670 over I2C at 0x%02X)", tp_io_cfg.dev_addr);
    } else {
        ESP_LOGE(TAG, "Touch init failed: %s", esp_err_to_name(ret));
    }
    return ret;
}
