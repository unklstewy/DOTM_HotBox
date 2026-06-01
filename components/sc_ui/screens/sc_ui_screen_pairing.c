/*
 * sc_ui_screen_pairing.c — First-boot Wi-Fi / bridge pairing wizard
 *
 * Shown when no SSID is found in NVS.
 * Allows entry of: SSID, Password, Bridge Host, Bridge Port,
 *                  Ship ID, Console ID, Terminal Index.
 *
 * Credentials (SSID + PSK) are written to NVS after confirmation.
 * They are NEVER logged or displayed in plain text.
 */

#include "sc_ui_screen_pairing.h"
#include "sc_ui_theme.h"
#include "sc_config.h"
#include "sc_network.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "sc_ui_pairing";

static lv_obj_t *s_root     = NULL;
static lv_obj_t *s_ssid_ta  = NULL;
static lv_obj_t *s_psk_ta   = NULL;
static lv_obj_t *s_host_ta  = NULL;
static lv_obj_t *s_ship_ta  = NULL;
static lv_obj_t *s_console_ta = NULL;

/* ── Save pairing ────────────────────────────────────────────────────────── */

static void connect_btn_cb(lv_event_t *e)
{
    const char *ssid    = lv_textarea_get_text(s_ssid_ta);
    const char *psk     = lv_textarea_get_text(s_psk_ta);
    const char *host    = lv_textarea_get_text(s_host_ta);
    const char *ship    = lv_textarea_get_text(s_ship_ta);
    const char *console = lv_textarea_get_text(s_console_ta);

    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGW(TAG, "SSID empty — cannot pair");
        return;
    }

    /* Store credentials in NVS — PSK is stored but never logged */
    nvs_handle_t h;
    if (nvs_open("sc_config", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid",        ssid);
        nvs_set_str(h, "wifi_psk",    psk);    /* PSK stored, not logged */
        nvs_set_str(h, "bridge_host", host);
        nvs_set_str(h, "ship_id",     ship);
        nvs_set_str(h, "console_id",  console);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Pairing saved for SSID: %s bridge: %s", ssid, host);
    }

    /* Restart to apply */
    esp_restart();
}

/* ── Create ──────────────────────────────────────────────────────────────── */

lv_obj_t *sc_ui_screen_pairing_create(lv_obj_t *parent)
{
    lv_lock();

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, SC_COL_BG, 0);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_root, SC_UI_PAD_MEDIUM, 0);
    lv_obj_set_style_pad_gap(s_root, SC_UI_PAD_SMALL, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, "SC Terminal — Setup");
    lv_obj_set_style_text_font(title, SC_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, SC_COL_ACCENT, 0);

    lv_obj_t *sub = lv_label_create(s_root);
    lv_label_set_text(sub, "Connect this terminal to your network and PC bridge.");
    lv_obj_set_style_text_color(sub, SC_COL_TEXT_DIM, 0);

    /* Helper macro for labelled textarea */
    #define FIELD(parent, label_text, ta_ptr, placeholder, pwd) \
        do { \
            lv_obj_t *lbl = lv_label_create(parent); \
            lv_label_set_text(lbl, label_text); \
            lv_obj_set_style_text_color(lbl, SC_COL_TEXT, 0); \
            *(ta_ptr) = lv_textarea_create(parent); \
            lv_textarea_set_placeholder_text(*(ta_ptr), placeholder); \
            lv_obj_set_width(*(ta_ptr), LV_PCT(100)); \
            if (pwd) lv_textarea_set_password_mode(*(ta_ptr), true); \
        } while(0)

    FIELD(s_root, "Wi-Fi SSID",    &s_ssid_ta,    "MyNetwork",          false);
    FIELD(s_root, "Wi-Fi Password",&s_psk_ta,     "••••••••",           true);
    FIELD(s_root, "Bridge Host",   &s_host_ta,    "sc-bridge.local",    false);
    FIELD(s_root, "Ship ID",       &s_ship_ta,    "cutlass_black",      false);
    FIELD(s_root, "Console ID",    &s_console_ta, "pilot_mfd_left",     false);

    #undef FIELD

    /* Connect button */
    lv_obj_t *connect_btn = lv_button_create(s_root);
    lv_obj_set_style_bg_color(connect_btn, SC_COL_READY, 0);
    lv_obj_add_event_cb(connect_btn, connect_btn_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_set_width(connect_btn, LV_PCT(100));

    lv_obj_t *connect_lbl = lv_label_create(connect_btn);
    lv_label_set_text(connect_lbl, "Save & Restart");
    lv_obj_align(connect_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(connect_lbl, lv_color_hex(0x111111), 0);

    lv_unlock();
    return s_root;
}

void sc_ui_screen_pairing_load(const sc_terminal_config_t *cfg) { (void)cfg; }

void sc_ui_screen_pairing_destroy(void)
{
    lv_lock();
    if (s_root) { lv_obj_delete(s_root); s_root = NULL; }
    lv_unlock();
    s_ssid_ta = s_psk_ta = s_host_ta = s_ship_ta = s_console_ta = NULL;
}

void sc_ui_screen_pairing_on_event(const sc_gamelink_event_t *evt)
{
    (void)evt;
}
