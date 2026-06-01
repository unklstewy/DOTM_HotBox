/*
 * sc_ui_screen_settings.c — Terminal settings screen
 *
 * Allows the user to:
 *   - Set ship_id / console_id / terminal_index
 *   - Set PC bridge host + port
 *   - Toggle HID enable
 *   - Trigger factory reset
 */

#include "sc_ui_screen_settings.h"
#include "sc_ui_theme.h"
#include "sc_config.h"
#include "esp_log.h"

static const char *TAG = "sc_ui_settings";

static lv_obj_t *s_root = NULL;

/* ── Save callback ───────────────────────────────────────────────────────── */
static void save_btn_cb(lv_event_t *e)
{
    /* TODO: read textarea values, call sc_config_save(), restart */
    ESP_LOGI(TAG, "Settings saved");
}

static void reset_btn_cb(lv_event_t *e)
{
    sc_config_factory_reset();
    ESP_LOGW(TAG, "Factory reset triggered");
}

/* ── Create ──────────────────────────────────────────────────────────────── */
lv_obj_t *sc_ui_screen_settings_create(lv_obj_t *parent)
{
    lv_lock();

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, SC_COL_BG, 0);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_root, SC_UI_PAD_MEDIUM, 0);
    lv_obj_set_style_pad_gap(s_root, SC_UI_PAD_MEDIUM, 0);

    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, "Terminal Settings");
    lv_obj_set_style_text_font(title, SC_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, SC_COL_ACCENT, 0);

    /* Bridge host */
    lv_obj_t *host_lbl = lv_label_create(s_root);
    lv_label_set_text(host_lbl, "Bridge Host");
    lv_obj_set_style_text_color(host_lbl, SC_COL_TEXT_DIM, 0);

    lv_obj_t *host_ta = lv_textarea_create(s_root);
    lv_textarea_set_placeholder_text(host_ta, "sc-bridge.local");
    lv_obj_set_width(host_ta, LV_PCT(100));

    /* Save / Reset buttons */
    lv_obj_t *btn_row = lv_obj_create(s_root);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(btn_row, SC_UI_PAD_MEDIUM, 0);

    lv_obj_t *save_btn = lv_button_create(btn_row);
    lv_obj_add_event_cb(save_btn, save_btn_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, "Save");

    lv_obj_t *reset_btn = lv_button_create(btn_row);
    lv_obj_set_style_bg_color(reset_btn, SC_COL_ARMED, 0);
    lv_obj_add_event_cb(reset_btn, reset_btn_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "Factory Reset");

    lv_unlock();
    return s_root;
}

void sc_ui_screen_settings_load(const sc_terminal_config_t *cfg) { (void)cfg; }

void sc_ui_screen_settings_destroy(void)
{
    lv_lock();
    if (s_root) { lv_obj_delete(s_root); s_root = NULL; }
    lv_unlock();
}

void sc_ui_screen_settings_on_event(const sc_gamelink_event_t *evt)
{
    (void)evt; /* Settings screen does not react to game events */
}
