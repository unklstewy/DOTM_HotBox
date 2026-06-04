/*
 * sc_ui_sprites.c — Sprite atlas loader and sub-image descriptor builder
 *
 * Loads one RGB565 atlas .bin from the SD card into PSRAM in a single read
 * (with taskYIELD() chunks to avoid WDT stalls).  Builds lv_image_dsc_t
 * descriptors whose .data pointers index directly into that buffer — no
 * per-sprite allocation, no pixel copies.
 *
 * Thread safety: sc_ui_sprites_load / unload must only be called from the
 * LVGL task (inside lv_lock) or during initialisation before the task starts.
 * sc_ui_sprites_get() is read-only and safe to call from any context once
 * the atlas is loaded.
 */

#include "sc_ui_sprites.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "sc_ui_sprites";

/* ── Atlas state ─────────────────────────────────────────────────────────── */

/* PSRAM buffer holding the raw RGB565 pixel data. */
static uint8_t              *s_atlas_buf  = NULL;
static size_t                s_atlas_size = 0;

/* Per-theme metadata saved at load time. */
static sc_ui_atlas_meta_t    s_meta;

/* Per-sprite rectangle table (indexed by sc_ui_sprite_id_t). */
static sc_ui_sprite_rect_t   s_rects[SC_SPRITE_COUNT];

/*
 * Pre-built LVGL image descriptors, one per sprite.
 * The .data field is set to point into s_atlas_buf; .data_size covers the
 * full rows spanned by the sprite (stride × height) so LVGL's internal
 * bounds checking passes.  The clip rect is conveyed via .header.w / .h.
 */
static lv_image_dsc_t s_descs[SC_SPRITE_COUNT];

/* Flag — true once s_atlas_buf is valid and s_descs are built. */
static bool s_loaded = false;

/* Standalone buffers for panel pieces to support proper tiling */
static uint8_t *s_panel_bufs[SC_SPRITE_PANEL_CENTER - SC_SPRITE_PANEL_TL + 1] = {NULL};

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Number of bytes to read per chunk before yielding to the RTOS scheduler. */
#define ATLAS_READ_CHUNK_BYTES (65536u)  /* 64 KB */

/**
 * Build one lv_image_dsc_t that references a sub-region of the atlas.
 *
 * LVGL's software renderer accesses image data as:
 *   row_ptr = data + row * stride
 * We set data to the start of the sprite's first row in the atlas,
 * and report w = sprite_w, h = sprite_h.  The stride (bytes per atlas row)
 * is encoded via cf / stride fields added in LVGL 9.
 */
static void s_build_descriptor(sc_ui_sprite_id_t id)
{
    const sc_ui_sprite_rect_t *r = &s_rects[id];
    lv_image_dsc_t            *d = &s_descs[id];
    uint32_t bytes_per_pixel     = (uint32_t)s_meta.color_depth / 8u;
    uint32_t atlas_stride        = (uint32_t)s_meta.width * bytes_per_pixel;

    /* Offset to the first pixel of this sprite within the atlas buffer. */
    uint32_t offset = (uint32_t)r->y * atlas_stride +
                      (uint32_t)r->x * bytes_per_pixel;

    d->header.magic     = LV_IMAGE_HEADER_MAGIC;
    d->header.cf        = LV_COLOR_FORMAT_RGB565;
    d->header.w         = r->w;
    d->header.h         = r->h;

    if (id >= SC_SPRITE_PANEL_TL && id <= SC_SPRITE_PANEL_CENTER) {
        /* Allocate standalone contiguous buffer to allow proper tiling stride support */
        uint32_t sprite_stride = (uint32_t)r->w * bytes_per_pixel;
        uint32_t size = (uint32_t)r->h * sprite_stride;
        uint8_t *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buf) {
            s_panel_bufs[id - SC_SPRITE_PANEL_TL] = buf;
            for (uint32_t row = 0; row < r->h; row++) {
                memcpy(buf + row * sprite_stride,
                       s_atlas_buf + offset + row * atlas_stride,
                       sprite_stride);
            }
            d->header.stride = (uint16_t)sprite_stride;
            d->data          = buf;
            d->data_size     = size;
        } else {
            ESP_LOGE(TAG, "Failed to allocate standalone buffer for panel sprite %d", id);
            d->header.stride = (uint16_t)atlas_stride;
            d->data          = s_atlas_buf + offset;
            d->data_size     = (uint32_t)r->h * atlas_stride;
        }
    } else {
        d->header.stride    = (uint16_t)atlas_stride; /* full-atlas row stride */
        d->data             = s_atlas_buf + offset;
        d->data_size        = (uint32_t)r->h * atlas_stride; /* rows accessible */
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t sc_ui_sprites_load(const char               *atlas_path,
                              const sc_ui_atlas_meta_t *meta,
                              const sc_ui_sprite_rect_t desc[SC_SPRITE_COUNT])
{
    if (!atlas_path || !meta || !desc) {
        ESP_LOGE(TAG, "NULL argument");
        return ESP_ERR_INVALID_ARG;
    }
    if (s_loaded) {
        ESP_LOGW(TAG, "Atlas already loaded — call unload() first");
        return ESP_ERR_INVALID_STATE;
    }

    /* Compute buffer size: width × height × bytes-per-pixel */
    if (meta->color_depth != 16) {
        ESP_LOGE(TAG, "Only RGB565 (16-bit) atlases are supported");
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_atlas_size = (size_t)meta->width * meta->height * 2u;

    /* Allocate in external PSRAM with DMA capability for LVGL renderer. */
    s_atlas_buf = heap_caps_malloc(s_atlas_size,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_atlas_buf) {
        ESP_LOGE(TAG, "PSRAM alloc failed for %zu bytes", s_atlas_size);
        s_atlas_size = 0;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Allocated %zu KB in PSRAM for atlas", s_atlas_size / 1024);

    /* Open the atlas file via LVGL's FS driver (SD card → FATFS). */
    lv_fs_file_t f;
    lv_fs_res_t  res = lv_fs_open(&f, atlas_path, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
        ESP_LOGE(TAG, "Cannot open atlas: %s (err %d)", atlas_path, (int)res);
        heap_caps_free(s_atlas_buf);
        s_atlas_buf  = NULL;
        s_atlas_size = 0;
        return ESP_ERR_NOT_FOUND;
    }

    /* Read in chunks, yielding between each to satisfy the WDT. */
    size_t   total_read = 0;
    uint8_t *dst        = s_atlas_buf;

    while (total_read < s_atlas_size) {
        uint32_t want = (uint32_t)(s_atlas_size - total_read);
        if (want > ATLAS_READ_CHUNK_BYTES) want = ATLAS_READ_CHUNK_BYTES;

        uint32_t got = 0;
        res = lv_fs_read(&f, dst, want, &got);
        if (res != LV_FS_RES_OK || got == 0) {
            ESP_LOGE(TAG, "Read error at offset %zu (got %"PRIu32")", total_read, got);
            lv_fs_close(&f);
            heap_caps_free(s_atlas_buf);
            s_atlas_buf  = NULL;
            s_atlas_size = 0;
            return ESP_FAIL;
        }
        total_read += got;
        dst        += got;
        taskYIELD(); /* keep WDT happy during large reads */
    }

    lv_fs_close(&f);
    ESP_LOGI(TAG, "Read %zu bytes from %s", total_read, atlas_path);

    /* Persist metadata and rect table. */
    s_meta = *meta;
    memcpy(s_rects, desc, sizeof(s_rects));

    /* Build per-sprite LVGL descriptors (pointer arithmetic into s_atlas_buf). */
    for (int i = 0; i < SC_SPRITE_COUNT; i++) {
        s_build_descriptor((sc_ui_sprite_id_t)i);
    }

    s_loaded = true;
    ESP_LOGI(TAG, "Atlas loaded: %"PRIu16"x%"PRIu16" RGB565, %d sprites",
             s_meta.width, s_meta.height, SC_SPRITE_COUNT);
    return ESP_OK;
}

void sc_ui_sprites_unload(void)
{
    if (!s_loaded) return;

    /* Zero the descriptors so stale pointers can't be dereferenced. */
    memset(s_descs, 0, sizeof(s_descs));

    /* Free standalone panel buffers */
    for (int i = 0; i <= (SC_SPRITE_PANEL_CENTER - SC_SPRITE_PANEL_TL); i++) {
        if (s_panel_bufs[i]) {
            heap_caps_free(s_panel_bufs[i]);
            s_panel_bufs[i] = NULL;
        }
    }

    heap_caps_free(s_atlas_buf);
    s_atlas_buf  = NULL;
    s_atlas_size = 0;
    s_loaded     = false;

    ESP_LOGI(TAG, "Atlas unloaded");
}

const lv_image_dsc_t *sc_ui_sprites_get(sc_ui_sprite_id_t id)
{
    if (!s_loaded) {
        ESP_LOGW(TAG, "sprites_get() called but no atlas is loaded");
        return NULL;
    }
    if ((unsigned)id >= SC_SPRITE_COUNT) {
        ESP_LOGE(TAG, "Invalid sprite id: %d", (int)id);
        return NULL;
    }
    return &s_descs[id];
}

esp_err_t sc_ui_sprites_get_rect(sc_ui_sprite_id_t    id,
                                  sc_ui_sprite_rect_t *rect)
{
    if (!s_loaded || (unsigned)id >= SC_SPRITE_COUNT || !rect) {
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
    return s_atlas_size;
}

sc_ui_sprite_id_t sc_ui_sprites_jog_frame(uint16_t angle_deg)
{
    /* 8 frames, each covering 45 degrees.  Round to nearest frame. */
    uint8_t frame = (uint8_t)(((uint32_t)angle_deg * 8u + 180u) / 360u) % 8u;
    return (sc_ui_sprite_id_t)((int)SC_SPRITE_JOG_WHEEL_F0 + frame);
}
