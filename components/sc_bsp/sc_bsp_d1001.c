#include "sc_bsp.h"
#include "sc_config.h"

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
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "sc_bsp_d1001";

/* ── Pin definitions (reTerminal D1001) ──────── */
#define SC_UI_BL_GPIO          (GPIO_NUM_14)
#define SC_UI_EXP_I2C_PORT     (I2C_NUM_1)
#define SC_UI_EXP_I2C_SCL      (GPIO_NUM_21)
#define SC_UI_EXP_I2C_SDA      (GPIO_NUM_20)
#define SC_UI_EXP_I2C_FREQ     (400000)
#define SC_UI_EXP_PWR_HOLD     (1ULL << 8)
#define SC_UI_EXP_LCD_PWR_EN   (1ULL << 0)
#define SC_UI_EXP_LCD_BL_EN    (1ULL << 7)
#define SC_UI_EXP_LCD_RST      (1ULL << 2)
#define SC_UI_EXP_TOUCH_RST    (1ULL << 12)
#define SC_UI_EXP_EN_READ_VBAT (1ULL << 6)  /* EXP_GPO6 controls VBAT FET */

#define SC_UI_TOUCH_I2C_PORT   (I2C_NUM_0)
#define SC_UI_TOUCH_SCL        (GPIO_NUM_38)
#define SC_UI_TOUCH_SDA        (GPIO_NUM_37)
#define SC_UI_TOUCH_INT        (GPIO_NUM_16)
#define SC_UI_TOUCH_I2C_FREQ   (400000)

#define SC_UI_PWR_BUTTON       (GPIO_NUM_3)
#define SC_UI_PWR_IN_VOLT      (GPIO_NUM_18)  /* READ_VBAT ADC — GPIO18 per schematic */
/* EN_READ_VBAT is driven by IO expander EXP_GPO6 — see SC_UI_EXP_EN_READ_VBAT */

#define SC_UI_DSI_PHY_LDO_CHAN       (3)
#define SC_UI_DSI_PHY_LDO_MV         (2500)
#define SC_UI_DPI_CLOCK_FREQ_MHZ     (60)
#define SC_UI_DSI_LANE_BITRATE_MBPS  (1000)
#define SC_UI_DISPLAY_WIDTH          (800)
#define SC_UI_DISPLAY_HEIGHT         (1280)
#define SC_UI_ENABLE_TOUCH           (1)
#define SC_UI_DBG_FLUSH_LOG_INITIAL  (8)
#define SC_UI_DBG_FLUSH_LOG_EVERY    (120)

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
#define SC_UI_DPI_COLOR_FORMAT LCD_COLOR_PIXEL_FORMAT_RGB565
#else
#define SC_UI_DPI_COLOR_FORMAT LCD_COLOR_FMT_RGB565
#endif

/* ── Hardware Handles ── */
static esp_ldo_channel_handle_t     s_dsi_ldo  = NULL;
static esp_lcd_dsi_bus_handle_t     s_dsi_bus  = NULL;
static esp_lcd_panel_io_handle_t    s_panel_io = NULL;
static esp_lcd_panel_handle_t       s_panel    = NULL;
static esp_lcd_touch_handle_t       s_touch    = NULL;
static i2c_master_bus_handle_t      s_exp_i2c_bus = NULL;
static esp_io_expander_handle_t     s_io_expander = NULL;
static void                        *s_panel_fb = NULL;
esp_io_expander_handle_t io_expander = NULL; /* Exported for GSL3670 driver */

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_adc_cali = NULL;
static adc_channel_t             s_adc_chan = 0;
static esp_timer_handle_t        s_pwr_timer = NULL;
static uint32_t                  s_btn_press_ms = 0;
static uint32_t                  s_last_release_ms = 0;
static sc_bsp_btn_triple_click_cb_t s_triple_click_cb = NULL;
static uint32_t                  s_click_count = 0;
static int                       s_battery_pct = 100;
static uint32_t                  s_flush_seq = 0;
static bool                      s_touch_last_pressed = false;

/* ── Forward declarations ── */
static esp_err_t sc_bsp_io_expander_init(void);
static esp_err_t sc_bsp_backlight_init(void);
static esp_err_t sc_bsp_display_hw_init(void);
static esp_err_t sc_bsp_touch_hw_init(void);
static void sc_bsp_power_hw_init(void);
static void sc_bsp_pwr_timer_cb(void *arg);
static void sc_bsp_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void sc_bsp_lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data);

/* ── Implementation ── */
esp_err_t sc_bsp_init(void)
{
    ESP_LOGI(TAG, "BSP init start (Seeed D1001)");

    sc_bsp_power_hw_init();

    ESP_LOGI(TAG, "Init PCA9535 IO expander (LCD power/reset/backlight gates)");
    esp_err_t ret = sc_bsp_io_expander_init();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Init backlight");
    ret = sc_bsp_backlight_init();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Init display");
    ret = sc_bsp_display_hw_init();
    if (ret != ESP_OK) return ret;

    if (SC_UI_ENABLE_TOUCH) {
        ESP_LOGI(TAG, "Init touch");
        ret = sc_bsp_touch_hw_init();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Touch init failed, continuing without touch: %s", esp_err_to_name(ret));
            s_touch = NULL;
        }
    }

    return ESP_OK;
}

lv_display_t* sc_bsp_display_create(void)
{
    if (!s_panel) return NULL;

    /* The hardware still needs its framebuffer to scan out from */
    void *panel_fb0 = NULL;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &panel_fb0));
    s_panel_fb = panel_fb0;
    ESP_LOGI(TAG, "Panel framebuffer at %p", panel_fb0);

    lv_display_t *disp = lv_display_create(SC_UI_DISPLAY_WIDTH, SC_UI_DISPLAY_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(disp, s_panel);
    lv_display_set_flush_cb(disp, sc_bsp_lvgl_flush_cb);

    uint32_t draw_buf_size = SC_UI_DISPLAY_WIDTH * SC_UI_DISPLAY_HEIGHT * 2 / 10;
    void *draw_buf = heap_caps_malloc(draw_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!draw_buf) {
        ESP_LOGW(TAG, "SRAM too small for draw buffer, falling back to PSRAM!");
        draw_buf = heap_caps_malloc(draw_buf_size, MALLOC_CAP_SPIRAM);
    }
    lv_display_set_buffers(disp, draw_buf, NULL, draw_buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    ESP_LOGI(TAG, "LVGL display configured (partial render, 1/10 screen draw buffer)");

    return disp;
}

lv_indev_t* sc_bsp_touch_create(void)
{
    if (!s_touch) return NULL;
    lv_indev_t *touch_indev = lv_indev_create();
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, sc_bsp_lvgl_touch_cb);
    return touch_indev;
}

void sc_bsp_power_off(void)
{
    if (s_io_expander) {
        esp_io_expander_set_level(s_io_expander, SC_UI_EXP_PWR_HOLD, 0);
    }
}

int sc_bsp_get_battery_pct(void)
{
    return s_battery_pct;
}

/* ── Callbacks ── */
static void sc_bsp_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    s_flush_seq++;
    if (s_flush_seq <= SC_UI_DBG_FLUSH_LOG_INITIAL || (s_flush_seq % SC_UI_DBG_FLUSH_LOG_EVERY) == 0) {
        ESP_LOGI(TAG, "Flush #%lu area=(%d,%d)-(%d,%d) rot=%d", (unsigned long)s_flush_seq,
                 (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2,
                 (int)lv_display_get_rotation(disp));
    }

    lv_display_rotation_t rotation = lv_display_get_rotation(disp);

    if (rotation == LV_DISPLAY_ROTATION_0 || !s_panel_fb) {
        esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    } else {
        uint16_t *src = (uint16_t *)px_map;
        uint16_t *dst = (uint16_t *)s_panel_fb;
        int w = area->x2 - area->x1 + 1;
        int h = area->y2 - area->y1 + 1;

        if (rotation == LV_DISPLAY_ROTATION_90) {
            for (int x = 0; x < w; x++) {
                int lx = area->x1 + x;
                int py = lx;
                int dst_base = py * 800;
                for (int y = 0; y < h; y++) {
                    int ly = area->y1 + y;
                    int px = 799 - ly;
                    dst[dst_base + px] = src[y * w + x];
                }
            }
        } else if (rotation == LV_DISPLAY_ROTATION_180) {
            for (int y = 0; y < h; y++) {
                int ly = area->y1 + y;
                int py = 1279 - ly;
                int dst_base = py * 800;
                for (int x = 0; x < w; x++) {
                    int lx = area->x1 + x;
                    int px = 799 - lx;
                    dst[dst_base + px] = src[y * w + x];
                }
            }
        } else if (rotation == LV_DISPLAY_ROTATION_270) {
            for (int x = 0; x < w; x++) {
                int lx = area->x1 + x;
                int py = 1279 - lx;
                int dst_base = py * 800;
                for (int y = 0; y < h; y++) {
                    int ly = area->y1 + y;
                    int px = ly;
                    dst[dst_base + px] = src[y * w + x];
                }
            }
        }
    }
    lv_display_flush_ready(disp);
}

static void sc_bsp_lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
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
        
        const sc_terminal_config_t *cfg = sc_config_get();
        if (cfg && cfg->touch_cal.is_calibrated) {
            if (cfg->touch_cal.swap_xy) {
                int32_t tmp = raw_x; raw_x = raw_y; raw_y = tmp;
            }
            int32_t dx = cfg->touch_cal.x_max - cfg->touch_cal.x_min;
            int32_t dy = cfg->touch_cal.y_max - cfg->touch_cal.y_min;
            if (dx != 0 && dy != 0) {
                raw_x = (raw_x - cfg->touch_cal.x_min) * SC_UI_DISPLAY_WIDTH / dx;
                raw_y = (raw_y - cfg->touch_cal.y_min) * SC_UI_DISPLAY_HEIGHT / dy;
            }
            if (cfg->touch_cal.invert_x) raw_x = SC_UI_DISPLAY_WIDTH - 1 - raw_x;
            if (cfg->touch_cal.invert_y) raw_y = SC_UI_DISPLAY_HEIGHT - 1 - raw_y;
        }

        data->point.x = raw_x;
        data->point.y = raw_y;
        data->state   = LV_INDEV_STATE_PRESSED;

        if (!s_touch_last_pressed) {
            ESP_LOGI(TAG, "Touch press raw=(%u,%u) cal=(%ld,%ld)",
                     (unsigned int)pt[0].x, (unsigned int)pt[0].y, (long)raw_x, (long)raw_y);
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    s_touch_last_pressed = pressed;
}

void sc_bsp_register_btn_triple_click_cb(sc_bsp_btn_triple_click_cb_t cb)
{
    s_triple_click_cb = cb;
}

static void sc_bsp_pwr_timer_cb(void *arg)
{
    static uint32_t s_ticks = 0;
    uint32_t now = esp_log_timestamp();
    int btn_state = gpio_get_level(SC_UI_PWR_BUTTON);

    /* Button logic (Active High) */
    if (btn_state == 1) {
        if (s_btn_press_ms == 0) {
            s_btn_press_ms = now;
        } else if (now - s_btn_press_ms > 2000) {
            ESP_LOGW(TAG, "Long press detected! Powering off.");
            sc_bsp_power_off();
            s_btn_press_ms = now + 10000;
        }
    } else {
        if (s_btn_press_ms > 0 && s_btn_press_ms <= now) {
            uint32_t duration = now - s_btn_press_ms;
            s_btn_press_ms = 0;
            if (duration < 500 && duration > 20) {
                if (s_click_count == 0 || now - s_last_release_ms < 500) {
                    s_click_count++;
                    s_last_release_ms = now;
                    ESP_LOGI(TAG, "Button click detected (count=%d)", (int)s_click_count);
                    if (s_click_count == 3) {
                        ESP_LOGW(TAG, "Triple tap detected! Triggering calibration...");
                        if (s_triple_click_cb) {
                            s_triple_click_cb();
                        }
                        s_click_count = 0;
                    }
                } else {
                    s_click_count = 1;
                    s_last_release_ms = now;
                }
            }
        } else {
            s_btn_press_ms = 0;
        }
    }

    if (s_click_count > 0 && now - s_last_release_ms >= 400) {
        if (s_click_count == 2) {
            ESP_LOGW(TAG, "Double tap detected! Restarting...");
            esp_restart();
        }
        s_click_count = 0;
    }

    s_ticks++;
    if (s_ticks % 100 == 0 && s_adc_handle && s_io_expander) {
        esp_io_expander_set_level(s_io_expander, SC_UI_EXP_EN_READ_VBAT, 1);
        int raw, mv = 0;
        if (adc_oneshot_read(s_adc_handle, s_adc_chan, &raw) == ESP_OK && s_adc_cali) {
            adc_cali_raw_to_voltage(s_adc_cali, raw, &mv);
        }
        esp_io_expander_set_level(s_io_expander, SC_UI_EXP_EN_READ_VBAT, 0);

        if (mv > 0) {
            int batt_mv = mv * 2;
            int pct = (batt_mv - 3200) / 10;
            if (pct > 100) pct = 100;
            if (pct < 0) pct = 0;
            s_battery_pct = pct;
        }
    }
}

/* ── HW Initializers ── */
static void sc_bsp_power_hw_init(void)
{
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << SC_UI_PWR_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_cfg);

    /* EN_READ_VBAT FET is controlled via IO expander (EXP_GPO6).
     * It is enabled/disabled in sc_bsp_pwr_timer_cb via s_io_expander.
     * No direct GPIO needed here. */

    adc_unit_t unit;
    if (adc_oneshot_io_to_channel(SC_UI_PWR_IN_VOLT, &unit, &s_adc_chan) == ESP_OK) {
        adc_oneshot_unit_init_cfg_t init_config = { .unit_id = unit, .ulp_mode = ADC_ULP_MODE_DISABLE };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc_handle));

        adc_oneshot_chan_cfg_t config = { .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12 };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, s_adc_chan, &config));

        adc_cali_curve_fitting_config_t cali_config = { .unit_id = unit, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
        adc_cali_create_scheme_curve_fitting(&cali_config, &s_adc_cali);
    }

    const esp_timer_create_args_t pwr_timer_args = { .callback = &sc_bsp_pwr_timer_cb, .name = "sc_bsp_pwr" };
    ESP_ERROR_CHECK(esp_timer_create(&pwr_timer_args, &s_pwr_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_pwr_timer, 50000));
}

static esp_err_t sc_bsp_io_expander_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = SC_UI_EXP_I2C_PORT, .sda_io_num = SC_UI_EXP_I2C_SDA,
        .scl_io_num = SC_UI_EXP_I2C_SCL, .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_exp_i2c_bus));
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_pca9535(s_exp_i2c_bus, ESP_IO_EXPANDER_I2C_PCA9535_ADDRESS_000, &s_io_expander));
    io_expander = s_io_expander;

    ESP_ERROR_CHECK(esp_io_expander_set_dir(s_io_expander, 0xFFFF, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_PWR_HOLD, 1));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_LCD_PWR_EN, 1));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_LCD_BL_EN, 1));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_LCD_RST, 1));
    
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_TOUCH_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_TOUCH_RST, 1));
    
    return ESP_OK;
}

static esp_err_t sc_bsp_backlight_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    const ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0, .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = SC_UI_BL_GPIO, .duty = 512, .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
    return ESP_OK;
}

static esp_err_t sc_bsp_display_hw_init(void)
{
    if (!s_dsi_ldo) {
        esp_ldo_channel_config_t ldo_cfg = { .chan_id = SC_UI_DSI_PHY_LDO_CHAN, .voltage_mv = SC_UI_DSI_PHY_LDO_MV };
        ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &s_dsi_ldo));
    }

    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0, .num_data_lanes = 2, .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = SC_UI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &s_dsi_bus));

    esp_lcd_dbi_io_config_t dbi_config = { .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_config, &s_panel_io));

    esp_lcd_dpi_panel_config_t dpi_config = JD9365_8_800_1280_PANEL_60HZ_DPI_CONFIG(SC_UI_DPI_COLOR_FORMAT);
    dpi_config.num_fbs = 1;

    jd9365_8_vendor_config_t vendor_config = { .mipi_config = { .dsi_bus = s_dsi_bus, .dpi_config = &dpi_config, .lane_num = 2 } };
    const esp_lcd_panel_dev_config_t panel_config = { .reset_gpio_num = -1, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16, .vendor_config = &vendor_config };

    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365_8(s_panel_io, &panel_config, &s_panel));

    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, SC_UI_EXP_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(120));

    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    return ESP_OK;
}

static esp_err_t sc_bsp_touch_hw_init(void)
{
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = SC_UI_TOUCH_I2C_PORT, .sda_io_num = SC_UI_TOUCH_SDA,
        .scl_io_num = SC_UI_TOUCH_SCL, .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &bus));

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GSL3670_CONFIG();
    tp_io_cfg.scl_speed_hz = SC_UI_TOUCH_I2C_FREQ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, &tp_io));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = SC_UI_DISPLAY_WIDTH, .y_max = SC_UI_DISPLAY_HEIGHT,
        .rst_gpio_num = -1, .int_gpio_num = SC_UI_TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 }, .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    
    return esp_lcd_touch_new_i2c_gsl3670(tp_io, &tp_cfg, &s_touch);
}

esp_err_t sc_bsp_brightness_set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t duty = (percent * 1023) / 100;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    return err;
}

