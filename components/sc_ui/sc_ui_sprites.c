/*
 * sc_ui_sprites.c — SVG rasterization engine and sprite cache
 */

#include "sc_ui_sprites.h"
#include "sc_ui_theme.h"
#include "sc_ui.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "cJSON.h"
#include "src/libs/svg/lv_svg.h"
#include "src/libs/svg/lv_svg_render.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "sc_ui_sprites";

/* ── Cache Structures ────────────────────────────────────────────────────── */

typedef struct {
    sc_ui_sprite_id_t id;
    uint16_t w;
    uint16_t h;
    uint8_t *pixel_buf;
    lv_image_dsc_t dsc;
} sc_cached_sprite_t;

#define MAX_CACHED_SPRITES 256
static sc_cached_sprite_t s_cache[MAX_CACHED_SPRITES];
static int s_cache_count = 0;

typedef struct {
    sc_ui_sprite_id_t id;
    uint16_t w;
    uint16_t h;
} sc_queued_sprite_t;

#define MAX_QUEUED_SPRITES 512
static sc_queued_sprite_t s_queue[MAX_QUEUED_SPRITES];
static int s_queue_count = 0;

/* ── Sprite Coordinates & Descriptors ────────────────────────────────────── */

static sc_ui_sprite_rect_t s_rects[SC_SPRITE_COUNT];
static lv_image_dsc_t s_descs[SC_SPRITE_COUNT];
static bool s_loaded = false;

/* SVG document parser variables */
static char *s_svg_content = NULL;
static char *s_svg_body = NULL;

/* ── String mapping ──────────────────────────────────────────────────────── */

static const struct {
    const char *name;
    sc_ui_sprite_id_t id;
} s_id_map[] = {
    { "btn_momentary_idle", SC_SPRITE_BTN_MOMENTARY_IDLE },
    { "btn_momentary_armed", SC_SPRITE_BTN_MOMENTARY_ARMED },
    { "btn_momentary_active", SC_SPRITE_BTN_MOMENTARY_ACTIVE },
    { "btn_latching_off", SC_SPRITE_BTN_LATCHING_OFF },
    { "btn_latching_on", SC_SPRITE_BTN_LATCHING_ON },
    { "btn_inactive", SC_SPRITE_BTN_INACTIVE },
    { "btn_danger", SC_SPRITE_BTN_DANGER },
    { "slider_track_h", SC_SPRITE_SLIDER_TRACK_H },
    { "slider_track_v", SC_SPRITE_SLIDER_TRACK_V },
    { "slider_thumb", SC_SPRITE_SLIDER_THUMB },
    { "axis_joystick_base", SC_SPRITE_AXIS_JOYSTICK_BASE },
    { "axis_joystick_thumb", SC_SPRITE_AXIS_JOYSTICK_THUMB },
    { "axis_dpad_base", SC_SPRITE_AXIS_DPAD_BASE },
    { "axis_dpad_up", SC_SPRITE_AXIS_DPAD_UP },
    { "axis_dpad_down", SC_SPRITE_AXIS_DPAD_DOWN },
    { "axis_dpad_left", SC_SPRITE_AXIS_DPAD_LEFT },
    { "axis_dpad_right", SC_SPRITE_AXIS_DPAD_RIGHT },
    { "axis_haat_base", SC_SPRITE_AXIS_HAAT_BASE },
    { "axis_haat_cursor", SC_SPRITE_AXIS_HAAT_CURSOR },
    { "axis_throttle_track", SC_SPRITE_AXIS_THROTTLE_TRACK },
    { "axis_throttle_grip", SC_SPRITE_AXIS_THROTTLE_GRIP },
    { "axis_yaw_ring", SC_SPRITE_AXIS_YAW_RING },
    { "axis_yaw_needle", SC_SPRITE_AXIS_YAW_NEEDLE },
    { "axis_rudder_track", SC_SPRITE_AXIS_RUDDER_TRACK },
    { "axis_rudder_pedal", SC_SPRITE_AXIS_RUDDER_PEDAL },
    { "knob_ring", SC_SPRITE_KNOB_RING },
    { "knob_cap", SC_SPRITE_KNOB_CAP },
    { "jog_wheel_f0", SC_SPRITE_JOG_WHEEL_F0 },
    { "jog_wheel_f1", SC_SPRITE_JOG_WHEEL_F1 },
    { "jog_wheel_f2", SC_SPRITE_JOG_WHEEL_F2 },
    { "jog_wheel_f3", SC_SPRITE_JOG_WHEEL_F3 },
    { "jog_wheel_f4", SC_SPRITE_JOG_WHEEL_F4 },
    { "jog_wheel_f5", SC_SPRITE_JOG_WHEEL_F5 },
    { "jog_wheel_f6", SC_SPRITE_JOG_WHEEL_F6 },
    { "jog_wheel_f7", SC_SPRITE_JOG_WHEEL_F7 },
    { "panel_tl", SC_SPRITE_PANEL_TL },
    { "panel_tr", SC_SPRITE_PANEL_TR },
    { "panel_bl", SC_SPRITE_PANEL_BL },
    { "panel_br", SC_SPRITE_PANEL_BR },
    { "panel_edge_t", SC_SPRITE_PANEL_EDGE_T },
    { "panel_edge_b", SC_SPRITE_PANEL_EDGE_B },
    { "panel_edge_l", SC_SPRITE_PANEL_EDGE_L },
    { "panel_edge_r", SC_SPRITE_PANEL_EDGE_R },
    { "panel_center", SC_SPRITE_PANEL_CENTER },
};

/* ── Internal Helpers ────────────────────────────────────────────────────── */

static void clear_cache(void)
{
    for (int i = 0; i < s_cache_count; i++) {
        if (s_cache[i].pixel_buf) {
            heap_caps_free(s_cache[i].pixel_buf);
            s_cache[i].pixel_buf = NULL;
        }
    }
    s_cache_count = 0;
}

static void add_to_cache(sc_ui_sprite_id_t id, uint16_t w, uint16_t h, uint8_t *pixel_buf)
{
    if (s_cache_count >= MAX_CACHED_SPRITES) {
        ESP_LOGE(TAG, "Cache full, cannot cache sprite %d (%dx%d)", id, w, h);
        heap_caps_free(pixel_buf);
        return;
    }

    // Check if it already exists in the cache
    for (int i = 0; i < s_cache_count; i++) {
        if (s_cache[i].id == id && s_cache[i].w == w && s_cache[i].h == h) {
            heap_caps_free(pixel_buf);
            return;
        }
    }

    sc_cached_sprite_t *entry = &s_cache[s_cache_count++];
    entry->id = id;
    entry->w = w;
    entry->h = h;
    entry->pixel_buf = pixel_buf;

    entry->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    entry->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    entry->dsc.header.w = w;
    entry->dsc.header.h = h;
    entry->dsc.header.stride = w * 2;
    entry->dsc.data = pixel_buf;
    entry->dsc.data_size = w * h * 2;
}

static void queue_sprite(sc_ui_sprite_id_t id, uint16_t w, uint16_t h)
{
    if (w == 0 || h == 0) return;
    if (s_queue_count >= MAX_QUEUED_SPRITES) return;
    
    // Deduplicate
    for (int i = 0; i < s_queue_count; i++) {
        if (s_queue[i].id == id && s_queue[i].w == w && s_queue[i].h == h) {
            return;
        }
    }
    s_queue[s_queue_count++] = (sc_queued_sprite_t){id, w, h};
}

static void extract_sprite_rects(const char *svg_data)
{
    memset(s_rects, 0, sizeof(s_rects));
    for (int i = 0; i < sizeof(s_id_map)/sizeof(s_id_map[0]); i++) {
        char target_id[64];
        snprintf(target_id, sizeof(target_id), "id=\"sprite-%s\"", s_id_map[i].name);
        char *p = strstr(svg_data, target_id);
        if (!p) {
            snprintf(target_id, sizeof(target_id), "id='sprite-%s'", s_id_map[i].name);
            p = strstr(svg_data, target_id);
        }
        if (p) {
            char *p_tag_end = strchr(p, '>');
            char *p_rect = strstr(p, "data-rect=\"");
            if (!p_rect) p_rect = strstr(p, "data-rect='");
            if (p_rect && (!p_tag_end || p_rect < p_tag_end)) {
                bool single_quote = (p_rect == strstr(p, "data-rect='"));
                p_rect += single_quote ? strlen("data-rect='") : strlen("data-rect=\"");
                int x=0, y=0, w=0, h=0;
                if (sscanf(p_rect, "%d,%d,%d,%d", &x, &y, &w, &h) == 4) {
                    s_rects[s_id_map[i].id].x = x;
                    s_rects[s_id_map[i].id].y = y;
                    s_rects[s_id_map[i].id].w = w;
                    s_rects[s_id_map[i].id].h = h;
                }
            }
        }
    }
}

static void queue_sprites_for_widget(const char *wtype, uint16_t target_w, uint16_t target_h)
{
    if (strcmp(wtype, "btn_momentary") == 0) {
        queue_sprite(SC_SPRITE_BTN_MOMENTARY_IDLE, target_w, target_h);
        queue_sprite(SC_SPRITE_BTN_MOMENTARY_ARMED, target_w, target_h);
        queue_sprite(SC_SPRITE_BTN_MOMENTARY_ACTIVE, target_w, target_h);
        queue_sprite(SC_SPRITE_BTN_INACTIVE, target_w, target_h);
    } else if (strcmp(wtype, "btn_latching") == 0) {
        queue_sprite(SC_SPRITE_BTN_LATCHING_OFF, target_w, target_h);
        queue_sprite(SC_SPRITE_BTN_LATCHING_ON, target_w, target_h);
        queue_sprite(SC_SPRITE_BTN_INACTIVE, target_w, target_h);
    } else if (strcmp(wtype, "btn_danger") == 0) {
        queue_sprite(SC_SPRITE_BTN_DANGER, target_w, target_h);
        queue_sprite(SC_SPRITE_BTN_MOMENTARY_ARMED, target_w, target_h);
        queue_sprite(SC_SPRITE_BTN_MOMENTARY_ACTIVE, target_w, target_h);
        queue_sprite(SC_SPRITE_BTN_INACTIVE, target_w, target_h);
    } else if (strcmp(wtype, "slider_h") == 0) {
        uint16_t track_w = target_w > 0 ? target_w : 120;
        uint16_t track_h = target_h > 0 ? target_h : 24;
        double scale_x = (double)track_w / 120.0;
        double scale_y = (double)track_h / 24.0;
        uint16_t thumb_w = (uint16_t)(40.0 * scale_x + 0.5);
        uint16_t thumb_h = (uint16_t)(24.0 * scale_y + 0.5);
        queue_sprite(SC_SPRITE_SLIDER_TRACK_H, track_w, track_h);
        queue_sprite(SC_SPRITE_SLIDER_THUMB, thumb_w, thumb_h);
    } else if (strcmp(wtype, "slider_v") == 0) {
        uint16_t track_w = target_w > 0 ? target_w : 24;
        uint16_t track_h = target_h > 0 ? target_h : 120;
        double scale_x = (double)track_w / 24.0;
        double scale_y = (double)track_h / 120.0;
        uint16_t thumb_w = (uint16_t)(24.0 * scale_x + 0.5);
        uint16_t thumb_h = (uint16_t)(40.0 * scale_y + 0.5);
        queue_sprite(SC_SPRITE_SLIDER_TRACK_V, track_w, track_h);
        queue_sprite(SC_SPRITE_SLIDER_THUMB, thumb_w, thumb_h);
    } else if (strcmp(wtype, "axis_joystick") == 0) {
        uint16_t base_w = target_w;
        uint16_t base_h = target_h;
        double scale_x = (double)base_w / 120.0;
        double scale_y = (double)base_h / 120.0;
        uint16_t thumb_w = (uint16_t)(40.0 * scale_x + 0.5);
        uint16_t thumb_h = (uint16_t)(40.0 * scale_y + 0.5);
        queue_sprite(SC_SPRITE_AXIS_JOYSTICK_BASE, base_w, base_h);
        queue_sprite(SC_SPRITE_AXIS_JOYSTICK_THUMB, thumb_w, thumb_h);
    } else if (strcmp(wtype, "axis_dpad") == 0) {
        uint16_t base_w = target_w;
        uint16_t base_h = target_h;
        double scale_x = (double)base_w / 120.0;
        double scale_y = (double)base_h / 120.0;
        uint16_t arrow_w = (uint16_t)(40.0 * scale_x + 0.5);
        uint16_t arrow_h = (uint16_t)(36.0 * scale_y + 0.5);
        queue_sprite(SC_SPRITE_AXIS_DPAD_BASE, base_w, base_h);
        queue_sprite(SC_SPRITE_AXIS_DPAD_UP, arrow_w, arrow_h);
        queue_sprite(SC_SPRITE_AXIS_DPAD_DOWN, arrow_w, arrow_h);
        queue_sprite(SC_SPRITE_AXIS_DPAD_LEFT, arrow_w, arrow_h);
        queue_sprite(SC_SPRITE_AXIS_DPAD_RIGHT, arrow_w, arrow_h);
    } else if (strcmp(wtype, "axis_haat") == 0) {
        uint16_t base_w = target_w;
        uint16_t base_h = target_h;
        double scale_x = (double)base_w / 120.0;
        double scale_y = (double)base_h / 120.0;
        uint16_t cursor_w = (uint16_t)(24.0 * scale_x + 0.5);
        uint16_t cursor_h = (uint16_t)(24.0 * scale_y + 0.5);
        queue_sprite(SC_SPRITE_AXIS_HAAT_BASE, base_w, base_h);
        queue_sprite(SC_SPRITE_AXIS_HAAT_CURSOR, cursor_w, cursor_h);
    } else if (strcmp(wtype, "axis_throttle") == 0) {
        uint16_t track_w = target_w;
        uint16_t track_h = target_h;
        double scale_x = (double)track_w / 44.0;
        double scale_y = (double)track_h / 120.0;
        uint16_t grip_w = (uint16_t)(60.0 * scale_x + 0.5);
        uint16_t grip_h = (uint16_t)(20.0 * scale_y + 0.5);
        queue_sprite(SC_SPRITE_AXIS_THROTTLE_TRACK, track_w, track_h);
        queue_sprite(SC_SPRITE_AXIS_THROTTLE_GRIP, grip_w, grip_h);
    } else if (strcmp(wtype, "axis_yaw") == 0) {
        uint16_t base_w = target_w;
        uint16_t base_h = target_h;
        double scale_x = (double)base_w / 120.0;
        double scale_y = (double)base_h / 120.0;
        uint16_t needle_w = (uint16_t)(10.0 * scale_x + 0.5);
        uint16_t needle_h = (uint16_t)(56.0 * scale_y + 0.5);
        queue_sprite(SC_SPRITE_AXIS_YAW_RING, base_w, base_h);
        queue_sprite(SC_SPRITE_AXIS_YAW_NEEDLE, needle_w, needle_h);
    } else if (strcmp(wtype, "axis_rudder") == 0) {
        uint16_t track_w = target_w;
        uint16_t track_h = target_h;
        double scale_x = (double)track_w / 256.0;
        double scale_y = (double)track_h / 32.0;
        uint16_t pedal_w = (uint16_t)(56.0 * scale_x + 0.5);
        uint16_t pedal_h = (uint16_t)(40.0 * scale_y + 0.5);
        queue_sprite(SC_SPRITE_AXIS_RUDDER_TRACK, track_w, track_h);
        queue_sprite(SC_SPRITE_AXIS_RUDDER_PEDAL, pedal_w, pedal_h);
    } else if (strcmp(wtype, "knob") == 0) {
        uint16_t base_w = target_w;
        uint16_t base_h = target_h;
        queue_sprite(SC_SPRITE_KNOB_RING, base_w, base_h);
        queue_sprite(SC_SPRITE_KNOB_CAP, base_w, base_h);
    } else if (strcmp(wtype, "jog_wheel") == 0) {
        uint16_t base_w = target_w;
        uint16_t base_h = target_h;
        for (int i = 0; i < 8; i++) {
            queue_sprite(SC_SPRITE_JOG_WHEEL_F0 + i, base_w, base_h);
        }
    } else {
        /* Unknown or missing widget_type — default to btn_momentary sprites so
         * old-schema ship JSONs (e.g. cutlass_black) still get correctly-sized
         * rasterised bitmaps rather than silently falling back to tiny defaults. */
        queue_sprite(SC_SPRITE_BTN_MOMENTARY_IDLE, target_w, target_h);
        queue_sprite(SC_SPRITE_BTN_MOMENTARY_ARMED, target_w, target_h);
        queue_sprite(SC_SPRITE_BTN_MOMENTARY_ACTIVE, target_w, target_h);
        queue_sprite(SC_SPRITE_BTN_INACTIVE, target_w, target_h);
    }
}

static uint8_t *rasterize_svg_cropped_to_rgb565(sc_ui_sprite_id_t id, uint16_t target_w, uint16_t target_h)
{
    sc_ui_sprite_rect_t src_rect = s_rects[id];
    if (src_rect.w == 0 || src_rect.h == 0) {
        ESP_LOGE(TAG, "Sprite %d has invalid source rect", id);
        return NULL;
    }

    if (target_w == 0) target_w = src_rect.w;
    if (target_h == 0) target_h = src_rect.h;

    char header_buf[256];
    snprintf(header_buf, sizeof(header_buf),
             "<svg width=\"%d\" height=\"%d\" viewBox=\"%d %d %d %d\" xmlns=\"http://www.w3.org/2000/svg\">",
             target_w, target_h, src_rect.x, src_rect.y, src_rect.w, src_rect.h);

    size_t header_len = strlen(header_buf);
    size_t body_len = strlen(s_svg_body);
    size_t total_len = header_len + body_len + 1;

    char *cropped_svg = heap_caps_malloc(total_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cropped_svg) {
        ESP_LOGE(TAG, "Failed to allocate memory for cropped SVG");
        return NULL;
    }
    strcpy(cropped_svg, header_buf);
    strcat(cropped_svg, s_svg_body);

    lv_lock();

    lv_svg_node_t *svg_node = lv_svg_load_data(cropped_svg, total_len - 1);
    heap_caps_free(cropped_svg);
    if (!svg_node) {
        ESP_LOGE(TAG, "Failed to load cropped SVG data");
        lv_unlock();
        return NULL;
    }

    lv_draw_buf_t *canvas_buf = lv_draw_buf_create(target_w, target_h, LV_COLOR_FORMAT_ARGB8888, 0);
    if (!canvas_buf) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer");
        lv_svg_node_delete(svg_node);
        lv_unlock();
        return NULL;
    }

    lv_obj_t *canvas = lv_canvas_create(lv_screen_active());
    if (!canvas) {
        ESP_LOGE(TAG, "Failed to create canvas object");
        lv_draw_buf_destroy(canvas_buf);
        lv_svg_node_delete(svg_node);
        lv_unlock();
        return NULL;
    }

    lv_canvas_set_draw_buf(canvas, canvas_buf);
    lv_canvas_fill_bg(canvas, lv_color_black(), 0);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN); /* don't render to screen */

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_svg(&layer, svg_node);

    /* Deadlock fix: lv_canvas_finish_layer holds lv_lock() while calling
     * lv_draw_wait_for_finish. The LVGL draw thread needs lv_lock() to run
     * (it calls lv_array_init internally). Holding the lock during the wait
     * means sc_splash_rast waits for the draw thread, and the draw thread
     * waits for sc_splash_rast — permanent deadlock, IDLE1 starved.
     *
     * Solution: dispatch draw tasks (lock held), then release before waiting,
     * reacquire after the draw thread has finished. */
    lv_draw_dispatch_layer(NULL, &layer);
    lv_unlock();                    /* draw thread can now acquire lv_lock() */
    lv_draw_wait_for_finish();  /* block until draw thread done */
    lv_lock();                      /* safe to touch LVGL objects again */

    lv_svg_node_delete(svg_node);
    lv_obj_delete(canvas);

    lv_unlock();


    size_t rgb_size = target_w * target_h * 2;
    uint8_t *rgb_buf = heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgb_buf) {
        ESP_LOGE(TAG, "Failed to allocate RGB565 buffer in PSRAM");
        lv_lock();
        lv_draw_buf_destroy(canvas_buf);
        lv_unlock();
        return NULL;
    }

    lv_color32_t *src_pixels = (lv_color32_t *)canvas_buf->data;
    uint16_t *dst_pixels = (uint16_t *)rgb_buf;
    uint32_t stride_pixels = canvas_buf->header.stride / 4;

    for (int y = 0; y < target_h; y++) {
        for (int x = 0; x < target_w; x++) {
            lv_color32_t p = src_pixels[y * stride_pixels + x];
            uint16_t rgb565;
            if (p.alpha < 128) {
                rgb565 = 0x0001; // Transparent chroma key
            } else {
                uint16_t r = p.red >> 3;
                uint16_t g = p.green >> 2;
                uint16_t b = p.blue >> 3;
                rgb565 = (r << 11) | (g << 5) | b;
                if (rgb565 == 0x0001) rgb565 = 0x0002;
            }
            dst_pixels[y * target_w + x] = rgb565;
        }
    }

    lv_lock();
    lv_draw_buf_destroy(canvas_buf);
    lv_unlock();

    return rgb_buf;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t sc_ui_sprites_load(const char               *atlas_path,
                              const sc_ui_atlas_meta_t *meta,
                              const sc_ui_sprite_rect_t desc[SC_SPRITE_COUNT])
{
    if (!atlas_path || !meta || !desc) return ESP_ERR_INVALID_ARG;

    FILE *f = fopen(atlas_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Atlas not found: %s", atlas_path);
        return ESP_ERR_NOT_FOUND;
    }

    size_t atlas_bytes = (size_t)meta->width * meta->height * 2;
    uint8_t *buf = heap_caps_malloc(atlas_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        fclose(f);
        ESP_LOGE(TAG, "Out of PSRAM for atlas (%zu bytes)", atlas_bytes);
        return ESP_ERR_NO_MEM;
    }

    size_t nread = fread(buf, 1, atlas_bytes, f);
    fclose(f);
    if (nread != atlas_bytes) {
        heap_caps_free(buf);
        ESP_LOGE(TAG, "Atlas read short: %zu / %zu", nread, atlas_bytes);
        return ESP_FAIL;
    }

    /* Build per-sprite descriptors as sub-views into the atlas buffer */
    for (int i = 0; i < SC_SPRITE_COUNT; i++) {
        uint16_t w = desc[i].w, h = desc[i].h;
        if (w == 0 || h == 0) continue;
        s_rects[i] = desc[i];
        /* Point data at the first row of this sprite inside the atlas */
        size_t row_bytes   = (size_t)meta->width * 2;
        size_t sprite_off  = (size_t)desc[i].y * row_bytes + (size_t)desc[i].x * 2;
        s_descs[i].header.magic  = LV_IMAGE_HEADER_MAGIC;
        s_descs[i].header.cf     = LV_COLOR_FORMAT_RGB565;
        s_descs[i].header.w      = w;
        s_descs[i].header.h      = h;
        s_descs[i].header.stride = (uint32_t)meta->width * 2; /* atlas row stride */
        s_descs[i].data          = buf + sprite_off;
        s_descs[i].data_size     = (size_t)h * meta->width * 2;
    }
    s_loaded = true;
    ESP_LOGI(TAG, "Atlas loaded: %s (%zu bytes PSRAM)", atlas_path, atlas_bytes);
    return ESP_OK;
}

/**
 * @brief Load per-sprite .bin files produced by tools/rasterize_sprites.py.
 *
 * Each file is a raw RGB565 bitmap (w×h×2 bytes, little-endian) stored at
 *   <theme_dir>/sprites/<sprite_name>.bin
 * A companion JSON at <theme_dir>/sprites/sprites_meta.json provides {w, h}
 * for each sprite name so the firmware knows the image dimensions without a
 * file header.
 *
 * This function has NO dependency on LVGL rendering and is safe to call from
 * any task — avoiding the lv_lock() / draw-thread deadlock that afflicts
 * the on-device SVG rasterizer.
 */
esp_err_t sc_ui_sprites_load_from_sdcard(const char        *theme_dir,
                                          void (*progress_cb)(int pct))
{
    /* ── 1. Read sprites_meta.json ──────────────────────────────────────── */
    char meta_path[128];
    snprintf(meta_path, sizeof(meta_path), "%s/sprites/sprites_meta.json", theme_dir);

    FILE *mf = fopen(meta_path, "r");
    if (!mf) {
        ESP_LOGE(TAG, "sprites_meta.json not found: %s", meta_path);
        return ESP_ERR_NOT_FOUND;
    }
    fseek(mf, 0, SEEK_END);
    long mlen = ftell(mf);
    rewind(mf);
    char *mbuf = heap_caps_malloc((size_t)mlen + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!mbuf) { fclose(mf); return ESP_ERR_NO_MEM; }
    fread(mbuf, 1, (size_t)mlen, mf);
    fclose(mf);
    mbuf[mlen] = '\0';

    cJSON *meta = cJSON_Parse(mbuf);
    heap_caps_free(mbuf);
    if (!meta) {
        ESP_LOGE(TAG, "Failed to parse sprites_meta.json");
        return ESP_FAIL;
    }

    /* Sprite name list — must match SPRITE_NAMES in rasterize_sprites.py */
    static const char * const SPRITE_FILE_NAMES[SC_SPRITE_COUNT] = {
        [SC_SPRITE_BTN_MOMENTARY_IDLE]   = "btn_momentary_idle",
        [SC_SPRITE_BTN_MOMENTARY_ARMED]  = "btn_momentary_armed",
        [SC_SPRITE_BTN_MOMENTARY_ACTIVE] = "btn_momentary_active",
        [SC_SPRITE_BTN_LATCHING_OFF]     = "btn_latching_off",
        [SC_SPRITE_BTN_LATCHING_ON]      = "btn_latching_on",
        [SC_SPRITE_BTN_INACTIVE]         = "btn_inactive",
        [SC_SPRITE_BTN_DANGER]           = "btn_danger",
        [SC_SPRITE_SLIDER_TRACK_H]       = "slider_track_h",
        [SC_SPRITE_SLIDER_TRACK_V]       = "slider_track_v",
        [SC_SPRITE_SLIDER_THUMB]         = "slider_thumb",
        [SC_SPRITE_AXIS_JOYSTICK_BASE]   = "axis_joystick_base",
        [SC_SPRITE_AXIS_JOYSTICK_THUMB]  = "axis_joystick_thumb",
        [SC_SPRITE_AXIS_DPAD_BASE]       = "axis_dpad_base",
        [SC_SPRITE_AXIS_DPAD_UP]         = "axis_dpad_up",
        [SC_SPRITE_AXIS_DPAD_DOWN]       = "axis_dpad_down",
        [SC_SPRITE_AXIS_DPAD_LEFT]       = "axis_dpad_left",
        [SC_SPRITE_AXIS_DPAD_RIGHT]      = "axis_dpad_right",
        [SC_SPRITE_AXIS_HAAT_BASE]       = "axis_haat_base",
        [SC_SPRITE_AXIS_HAAT_CURSOR]     = "axis_haat_cursor",
        [SC_SPRITE_AXIS_THROTTLE_TRACK]  = "axis_throttle_track",
        [SC_SPRITE_AXIS_THROTTLE_GRIP]   = "axis_throttle_grip",
        [SC_SPRITE_AXIS_YAW_RING]        = "axis_yaw_ring",
        [SC_SPRITE_AXIS_YAW_NEEDLE]      = "axis_yaw_needle",
        [SC_SPRITE_AXIS_RUDDER_TRACK]    = "axis_rudder_track",
        [SC_SPRITE_AXIS_RUDDER_PEDAL]    = "axis_rudder_pedal",
        [SC_SPRITE_KNOB_RING]            = "knob_ring",
        [SC_SPRITE_KNOB_CAP]             = "knob_cap",
        [SC_SPRITE_JOG_WHEEL_F0]         = "jog_wheel_f0",
        [SC_SPRITE_JOG_WHEEL_F1]         = "jog_wheel_f1",
        [SC_SPRITE_JOG_WHEEL_F2]         = "jog_wheel_f2",
        [SC_SPRITE_JOG_WHEEL_F3]         = "jog_wheel_f3",
        [SC_SPRITE_JOG_WHEEL_F4]         = "jog_wheel_f4",
        [SC_SPRITE_JOG_WHEEL_F5]         = "jog_wheel_f5",
        [SC_SPRITE_JOG_WHEEL_F6]         = "jog_wheel_f6",
        [SC_SPRITE_JOG_WHEEL_F7]         = "jog_wheel_f7",
        [SC_SPRITE_PANEL_TL]             = "panel_tl",
        [SC_SPRITE_PANEL_TR]             = "panel_tr",
        [SC_SPRITE_PANEL_BL]             = "panel_bl",
        [SC_SPRITE_PANEL_BR]             = "panel_br",
        [SC_SPRITE_PANEL_EDGE_T]         = "panel_edge_t",
        [SC_SPRITE_PANEL_EDGE_B]         = "panel_edge_b",
        [SC_SPRITE_PANEL_EDGE_L]         = "panel_edge_l",
        [SC_SPRITE_PANEL_EDGE_R]         = "panel_edge_r",
        [SC_SPRITE_PANEL_CENTER]         = "panel_center",
    };

    /* ── 2. Load each sprite .bin into PSRAM ────────────────────────────── */
    int loaded = 0;
    for (int i = 0; i < SC_SPRITE_COUNT; i++) {
        const char *sname = SPRITE_FILE_NAMES[i];
        if (!sname) { continue; }

        /* Read w/h from meta JSON */
        cJSON *entry = cJSON_GetObjectItem(meta, sname);
        if (!entry) { continue; }
        int w = cJSON_GetObjectItem(entry, "w") ?
                cJSON_GetObjectItem(entry, "w")->valueint : 0;
        int h = cJSON_GetObjectItem(entry, "h") ?
                cJSON_GetObjectItem(entry, "h")->valueint : 0;
        if (w == 0 || h == 0) { continue; }

        char bin_path[160];
        snprintf(bin_path, sizeof(bin_path), "%s/sprites/%s.bin", theme_dir, sname);

        FILE *bf = fopen(bin_path, "rb");
        if (!bf) {
            ESP_LOGW(TAG, "Sprite missing: %s", bin_path);
            continue;
        }

        size_t nbytes = (size_t)w * h * 2;
        uint8_t *buf = heap_caps_malloc(nbytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) {
            fclose(bf);
            ESP_LOGE(TAG, "PSRAM OOM loading %s", sname);
            continue;
        }

        size_t nread = fread(buf, 1, nbytes, bf);
        fclose(bf);
        if (nread != nbytes) {
            heap_caps_free(buf);
            ESP_LOGW(TAG, "Short read for %s (%zu/%zu)", sname, nread, nbytes);
            continue;
        }

        s_rects[i] = (sc_ui_sprite_rect_t){ .x=0, .y=0, .w=(uint16_t)w, .h=(uint16_t)h };
        s_descs[i].header.magic  = LV_IMAGE_HEADER_MAGIC;
        s_descs[i].header.cf     = LV_COLOR_FORMAT_RGB565;
        s_descs[i].header.w      = (uint16_t)w;
        s_descs[i].header.h      = (uint16_t)h;
        s_descs[i].header.stride = (uint32_t)w * 2;
        s_descs[i].data          = buf;
        s_descs[i].data_size     = nbytes;

        /* Also insert into the scaled cache so get_scaled() finds it */
        add_to_cache(i, (uint16_t)w, (uint16_t)h, buf);
        loaded++;

        if (progress_cb) {
            progress_cb((loaded * 100) / SC_SPRITE_COUNT);
        }
        taskYIELD(); /* keep IDLE task healthy during SD card reads */
    }

    cJSON_Delete(meta);
    s_loaded = (loaded > 0);
    ESP_LOGI(TAG, "Loaded %d/%d sprites from %s/sprites/", loaded, SC_SPRITE_COUNT, theme_dir);
    return s_loaded ? ESP_OK : ESP_ERR_NOT_FOUND;
}


void sc_ui_sprites_unload(void)
{
    clear_cache();
    s_loaded = false;
    ESP_LOGI(TAG, "Sprite cache unloaded");
}

const lv_image_dsc_t *sc_ui_sprites_get(sc_ui_sprite_id_t id)
{
    return sc_ui_sprites_get_scaled(id, 0, 0);
}

esp_err_t sc_ui_sprites_get_rect(sc_ui_sprite_id_t    id,
                                  sc_ui_sprite_rect_t *rect)
{
    if ((unsigned)id >= SC_SPRITE_COUNT || !rect) {
        return ESP_ERR_INVALID_ARG;
    }
    *rect = s_rects[id];
    return ESP_OK;
}

bool sc_ui_sprites_is_loaded(void)
{
    return s_loaded;
}

size_t sc_ui_sprites_atlas_size(void)
{
    size_t total = 0;
    for (int i = 0; i < s_cache_count; i++) {
        if (s_cache[i].pixel_buf) {
            total += s_cache[i].dsc.data_size;
        }
    }
    return total;
}

sc_ui_sprite_id_t sc_ui_sprites_jog_frame(uint16_t angle_deg)
{
    uint8_t frame = (uint8_t)(((uint32_t)angle_deg * 8u + 180u) / 360u) % 8u;
    return (sc_ui_sprite_id_t)((int)SC_SPRITE_JOG_WHEEL_F0 + frame);
}

esp_err_t sc_ui_sprites_rasterize_all(const char *ship_id, void (*progress_cb)(int pct))
{
    if (!ship_id) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting SVG sprite rasterizer for ship layout: %s", ship_id);

    clear_cache();

    // 1. Open and parse ship JSON first to check manufacturer & setup theme
    char path[128];
    snprintf(path, sizeof(path), "/sdcard/ships/%s.json", ship_id);
    FILE *f_json = fopen(path, "r");
    if (!f_json) {
        ESP_LOGE(TAG, "Failed to open ship JSON: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f_json, 0, SEEK_END);
    size_t json_sz = ftell(f_json);
    fseek(f_json, 0, SEEK_SET);
    char *json_buf = malloc(json_sz + 1);
    if (!json_buf) {
        fclose(f_json);
        return ESP_ERR_NO_MEM;
    }
    fread(json_buf, 1, json_sz, f_json);
    json_buf[json_sz] = '\0';
    fclose(f_json);

    cJSON *root = cJSON_Parse(json_buf);
    free(json_buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parsing failed for layout %s", ship_id);
        return ESP_FAIL;
    }

    cJSON *manuf = cJSON_GetObjectItem(root, "manufacturer");
    if (manuf && cJSON_IsString(manuf)) {
        char manuf_lower[64] = {0};
        for (int i = 0; manuf->valuestring[i] && i < sizeof(manuf_lower) - 1; i++) {
            char c = manuf->valuestring[i];
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
            manuf_lower[i] = c;
        }
        if (strstr(manuf_lower, "origin")) {
            sc_ui_theme_init_origin_lux();
        } else {
            sc_ui_theme_init_drake_military();
        }
    } else {
        sc_ui_theme_init_drake_military();
    }

    // 2. Open and load correct theme SVG
    sc_theme_id_t active_theme = sc_ui_theme_get_active();
    const char *svg_path = "/sdcard/assets/themes/drake/sprite_sheet.svg";
    if (active_theme == SC_THEME_ORIGIN_LUX) {
        svg_path = "/sdcard/assets/themes/origin/sprite_sheet.svg";
    }

    FILE *f_svg = fopen(svg_path, "r");
    if (!f_svg) {
        ESP_LOGE(TAG, "Failed to open SVG file: %s", svg_path);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f_svg, 0, SEEK_END);
    size_t svg_sz = ftell(f_svg);
    fseek(f_svg, 0, SEEK_SET);

    s_svg_content = heap_caps_malloc(svg_sz + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_svg_content) {
        fclose(f_svg);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    fread(s_svg_content, 1, svg_sz, f_svg);
    s_svg_content[svg_sz] = '\0';
    fclose(f_svg);

    char *p_svg_start = strstr(s_svg_content, "<svg");
    if (p_svg_start) {
        char *p_svg_end = strchr(p_svg_start, '>');
        if (p_svg_end) {
            s_svg_body = p_svg_end + 1;
        }
    }
    if (!s_svg_body) {
        ESP_LOGE(TAG, "Invalid SVG file root element structure");
        heap_caps_free(s_svg_content);
        s_svg_content = NULL;
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    extract_sprite_rects(s_svg_content);

    s_queue_count = 0;

    cJSON *consoles = cJSON_GetObjectItem(root, "consoles");
    if (consoles && cJSON_IsArray(consoles)) {
        int consoles_cnt = cJSON_GetArraySize(consoles);
        for (int c_idx = 0; c_idx < consoles_cnt; c_idx++) {
            cJSON *console = cJSON_GetArrayItem(consoles, c_idx);
            if (!console) continue;
            cJSON *layout_item = cJSON_GetObjectItem(console, "layout");
            const char *layout = layout_item ? layout_item->valuestring : "grid_4x5";
            int cols = 4, rows = 5;
            sscanf(layout, "grid_%dx%d", &cols, &rows);
            if (cols <= 0) cols = 4;
            if (rows <= 0) rows = 5;

            int pad_x = 16, pad_y = 16;
            int total_w = 800 - 2 * pad_x - 32;
            int total_h = 1280 - 2 * pad_y - 100 - 32;
            int cell_w = total_w / cols;
            int cell_h = total_h / rows;

            cJSON *actions = cJSON_GetObjectItem(console, "actions");
            if (actions && cJSON_IsArray(actions)) {
                int actions_cnt = cJSON_GetArraySize(actions);
                for (int a_idx = 0; a_idx < actions_cnt; a_idx++) {
                    cJSON *action = cJSON_GetArrayItem(actions, a_idx);
                    if (!action) continue;
                    cJSON *wtype_item = cJSON_GetObjectItem(action, "widget_type");
                    cJSON *w_item = cJSON_GetObjectItem(action, "width");
                    cJSON *h_item = cJSON_GetObjectItem(action, "height");
                    const char *wtype = wtype_item ? wtype_item->valuestring : "";
                    int w_cells = w_item ? w_item->valueint : 1;
                    int h_cells = h_item ? h_item->valueint : 1;

                    int target_w = cell_w * w_cells;
                    int target_h = cell_h * h_cells;

                    queue_sprites_for_widget(wtype, target_w, target_h);
                }
            }
        }
    }
    cJSON_Delete(root);

    int total_steps = SC_SPRITE_COUNT + s_queue_count;
    int current_step = 0;

    // 1. Rasterize default sizes and save in s_descs
    for (int i = 0; i < SC_SPRITE_COUNT; i++) {
        uint16_t dw = s_rects[i].w;
        uint16_t dh = s_rects[i].h;
        if (dw > 0 && dh > 0) {
            uint8_t *buf = rasterize_svg_cropped_to_rgb565(i, dw, dh);
            if (buf) {
                add_to_cache(i, dw, dh, buf);
                s_descs[i].header.magic = LV_IMAGE_HEADER_MAGIC;
                s_descs[i].header.cf = LV_COLOR_FORMAT_RGB565;
                s_descs[i].header.w = dw;
                s_descs[i].header.h = dh;
                s_descs[i].header.stride = dw * 2;
                s_descs[i].data = buf;
                s_descs[i].data_size = dw * dh * 2;
            }
        }
        current_step++;
        if (progress_cb) {
            progress_cb((current_step * 100) / total_steps);
        }
        /* Yield to IDLE1 so the task watchdog can be serviced and the LVGL
         * task gets a chance to run between sprites. */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // 2. Rasterize custom sizes in queue
    for (int i = 0; i < s_queue_count; i++) {
        sc_queued_sprite_t q = s_queue[i];
        uint8_t *buf = rasterize_svg_cropped_to_rgb565(q.id, q.w, q.h);
        if (buf) {
            add_to_cache(q.id, q.w, q.h, buf);
        }
        current_step++;
        if (progress_cb) {
            progress_cb((current_step * 100) / total_steps);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    heap_caps_free(s_svg_content);
    s_svg_content = NULL;
    s_svg_body = NULL;

    s_loaded = true;
    ESP_LOGI(TAG, "Rasterization complete! %d cache entries active.", s_cache_count);
    return ESP_OK;
}

const lv_image_dsc_t *sc_ui_sprites_get_scaled(sc_ui_sprite_id_t id, uint16_t w, uint16_t h)
{
    if ((unsigned)id >= SC_SPRITE_COUNT) {
        return NULL;
    }

    if (w == 0 || h == 0) {
        w = s_rects[id].w;
        h = s_rects[id].h;
    }

    // Search the cache
    for (int i = 0; i < s_cache_count; i++) {
        if (s_cache[i].id == id && s_cache[i].w == w && s_cache[i].h == h) {
            return &s_cache[i].dsc;
        }
    }

    return &s_descs[id];
}
