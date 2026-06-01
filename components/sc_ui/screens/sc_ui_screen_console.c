/*
 * sc_ui_screen_console.c — Ship console MFD layout renderer
 *
 * Reads the active console's action array (loaded by sc_config), builds
 * a grid of touch buttons, and wires each button to sc_hid_action_send().
 *
 * Button states update in response to sc_gamelink events.
 */

#include "sc_ui_screen_console.h"
#include "sc_ui_theme.h"
#include "sc_config.h"
#include "sc_hid.h"
#include "sc_gamelink.h"

#include "cJSON.h"
#include "esp_log.h"

#include <string.h>

#include "lvgl.h"

static const char *TAG = "sc_ui_console";

/* ── State ───────────────────────────────────────────────────────────────── */
static lv_obj_t *s_root    = NULL;
static lv_obj_t *s_grid    = NULL;
static lv_obj_t *s_title   = NULL;

/* Max buttons per console */
#define MAX_BUTTONS (32)

typedef struct {
    char      action_id[32];
    lv_obj_t *btn;
    lv_obj_t *label;
    char      state_event[48];           /* gamelink event name, or ""  */
    /* State colours from JSON */
    char      state_keys[4][16];         /* e.g. "up", "down"           */
    lv_color_t state_colors[4];
    char      state_labels[4][16];
    uint8_t   state_count;
} console_btn_t;

static console_btn_t s_buttons[MAX_BUTTONS];
static int           s_btn_count = 0;

/* ── Button event callback ───────────────────────────────────────────────── */

static void btn_released_cb(lv_event_t *e)
{
    const console_btn_t *btn = (const console_btn_t *)lv_event_get_user_data(e);
    if (!btn) return;
    ESP_LOGD(TAG, "Button pressed: %s", btn->action_id);
    esp_err_t ret = sc_hid_action_send(btn->action_id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HID send failed: %s", esp_err_to_name(ret));
    }
}

/* ── Parse colour from JSON hex string ────────────────────────────────────── */

static lv_color_t parse_hex_color(const char *hex)
{
    if (!hex || hex[0] != '#') return SC_COL_OFF;
    unsigned long val = strtoul(hex + 1, NULL, 16);
    return lv_color_hex((uint32_t)val);
}

/* ── Create screen ───────────────────────────────────────────────────────── */

lv_obj_t *sc_ui_screen_console_create(lv_obj_t *parent)
{
    lv_lock();

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 800, 1280);
    lv_obj_set_style_bg_color(s_root, SC_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, SC_UI_PAD_SMALL, 0);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);

    /* Title bar */
    s_title = lv_label_create(s_root);
    lv_label_set_text(s_title, "SC Terminal");
    lv_obj_set_style_text_color(s_title, SC_COL_ACCENT, 0);
    lv_obj_set_style_text_font(s_title, SC_FONT_TITLE, 0);
    lv_obj_set_style_pad_bottom(s_title, SC_UI_PAD_MEDIUM, 0);

    /* Button grid */
    s_grid = lv_obj_create(s_root);
    lv_obj_set_size(s_grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_grid, 0, 0);
    lv_obj_set_style_pad_all(s_grid, SC_UI_PAD_SMALL, 0);
    lv_obj_set_style_pad_gap(s_grid, SC_UI_PAD_SMALL, 0);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);

    /* Populate from s_buttons (pre-loaded by sc_ui_screen_console_load) */
    for (int i = 0; i < s_btn_count; i++) {
        console_btn_t *cb = &s_buttons[i];

        lv_obj_t *btn = lv_button_create(s_grid);
        lv_obj_set_size(btn, 160, 80);
        lv_obj_set_style_bg_color(btn, SC_COL_BG_PANEL, 0);
        lv_obj_set_style_bg_color(btn, SC_COL_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, SC_UI_BTN_RADIUS, 0);
        lv_obj_add_event_cb(btn, btn_released_cb, LV_EVENT_RELEASED, cb);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, cb->action_id); /* overwritten by load() */
        lv_obj_set_style_text_color(lbl, SC_COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl, SC_FONT_MEDIUM, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

        cb->btn   = btn;
        cb->label = lbl;
    }

    lv_unlock();
    return s_root;
}

/* ── Load console from config ────────────────────────────────────────────── */

void sc_ui_screen_console_load(const sc_terminal_config_t *cfg)
{
    ESP_LOGI(TAG, "Loading console: %s / %s", cfg->ship_id, cfg->console_id);

    /* Load ship JSON */
    char *json_str = NULL;
    size_t json_len = 0;
    if (sc_config_ship_json_load(&json_str, &json_len) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load ship JSON");
        return;
    }

    cJSON *root = cJSON_ParseWithLength(json_str, json_len);
    free(json_str);
    if (!root) { ESP_LOGE(TAG, "JSON parse error"); return; }

    /* Find the matching console */
    cJSON *consoles = cJSON_GetObjectItem(root, "consoles");
    cJSON *console  = NULL;
    cJSON *c = NULL;
    cJSON_ArrayForEach(c, consoles) {
        cJSON *cid = cJSON_GetObjectItem(c, "console_id");
        if (cJSON_IsString(cid) &&
            strcmp(cid->valuestring, cfg->console_id) == 0) {
            console = c;
            break;
        }
    }
    if (!console) {
        ESP_LOGW(TAG, "Console '%s' not found in ship JSON", cfg->console_id);
        cJSON_Delete(root);
        return;
    }

    /* Update title */
    cJSON *disp_name = cJSON_GetObjectItem(console, "display_name");
    if (s_title && cJSON_IsString(disp_name)) {
        lv_lock();
        lv_label_set_text(s_title, disp_name->valuestring);
        lv_unlock();
    }

    /* Build button list */
    s_btn_count = 0;
    cJSON *actions = cJSON_GetObjectItem(console, "actions");
    cJSON *action  = NULL;
    cJSON_ArrayForEach(action, actions) {
        if (s_btn_count >= MAX_BUTTONS) break;
        console_btn_t *cb = &s_buttons[s_btn_count];
        memset(cb, 0, sizeof(*cb));

        cJSON *id    = cJSON_GetObjectItem(action, "id");
        cJSON *label = cJSON_GetObjectItem(action, "label");
        cJSON *state = cJSON_GetObjectItem(action, "state");

        if (!cJSON_IsString(id)) continue;
        strlcpy(cb->action_id, id->valuestring, sizeof(cb->action_id));

        /* Update label on the existing button widget if already created */
        if (cb->label && cJSON_IsString(label)) {
            lv_lock();
            lv_label_set_text(cb->label, label->valuestring);
            lv_unlock();
        }

        /* Parse gamelink state config */
        if (cJSON_IsObject(state)) {
            cJSON *evt_item = cJSON_GetObjectItem(state, "gamelink_event");
            if (cJSON_IsString(evt_item)) {
                strlcpy(cb->state_event, evt_item->valuestring,
                        sizeof(cb->state_event));
                /* Register gamelink handler for this button */
                sc_gamelink_handler_register(cb->state_event,
                    (sc_gamelink_handler_fn_t)sc_ui_screen_console_on_event,
                    NULL);
            }
            cJSON *values = cJSON_GetObjectItem(state, "values");
            if (cJSON_IsObject(values)) {
                cJSON *v = NULL;
                cJSON_ArrayForEach(v, values) {
                    if (cb->state_count >= 4) break;
                    int idx = cb->state_count++;
                    strlcpy(cb->state_keys[idx], v->string,
                            sizeof(cb->state_keys[idx]));
                    cJSON *lbl_j = cJSON_GetObjectItem(v, "label");
                    cJSON *col_j = cJSON_GetObjectItem(v, "color");
                    if (cJSON_IsString(lbl_j))
                        strlcpy(cb->state_labels[idx], lbl_j->valuestring,
                                sizeof(cb->state_labels[idx]));
                    if (cJSON_IsString(col_j))
                        cb->state_colors[idx] = parse_hex_color(col_j->valuestring);
                }
            }
        }

        /* Load into HID action table */
        cJSON *hid = cJSON_GetObjectItem(action, "hid");
        if (cJSON_IsObject(hid)) {
            sc_hid_action_t ha = {0};
            strlcpy(ha.action_id, cb->action_id, sizeof(ha.action_id));
            cJSON *mod  = cJSON_GetObjectItem(hid, "modifier");
            cJSON *keys = cJSON_GetObjectItem(hid, "keycodes");
            cJSON *cons = cJSON_GetObjectItem(hid, "consumer_usage");
            cJSON *hold = cJSON_GetObjectItem(hid, "hold_ms");
            if (cJSON_IsNumber(mod))  ha.modifier       = (uint8_t)mod->valueint;
            if (cJSON_IsNumber(cons)) ha.consumer_usage = (uint16_t)cons->valueint;
            if (cJSON_IsNumber(hold)) ha.hold_ms        = (uint32_t)hold->valueint;
            if (cJSON_IsArray(keys)) {
                int ki = 0;
                cJSON *k = NULL;
                cJSON_ArrayForEach(k, keys) {
                    if (ki >= 6) break;
                    ha.keycodes[ki++] = (uint8_t)k->valueint;
                }
            }
            /* Note: actions accumulated then loaded as a bulk table
             * after the loop. For simplicity we call per-action here. */
            sc_hid_action_table_load(&ha, 1);
        }

        s_btn_count++;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Console loaded: %d buttons", s_btn_count);
}

/* ── Destroy ─────────────────────────────────────────────────────────────── */

void sc_ui_screen_console_destroy(void)
{
    lv_lock();
    if (s_root) {
        lv_obj_delete(s_root);
        s_root  = NULL;
        s_grid  = NULL;
        s_title = NULL;
    }
    lv_unlock();
    s_btn_count = 0;
}

/* ── Gamelink event handler ──────────────────────────────────────────────── */

void sc_ui_screen_console_on_event(const sc_gamelink_event_t *evt)
{
    if (!s_root || !evt) return;

    /* Find buttons that care about this event */
    for (int i = 0; i < s_btn_count; i++) {
        console_btn_t *cb = &s_buttons[i];
        if (strcmp(cb->state_event, evt->event_id) != 0) continue;

        /* Determine new state from payload — use raw for now */
        const char *state_val = evt->payload.raw;
        /* For typed events, use the named fields: */
        if (strcmp(evt->event_id, SC_GAMELINK_EVT_GEAR_STATE) == 0) {
            state_val = evt->payload.gear.state;
        }

        /* Find matching state entry */
        for (int s = 0; s < cb->state_count; s++) {
            if (strcmp(cb->state_keys[s], state_val) == 0) {
                lv_lock();
                lv_label_set_text(cb->label, cb->state_labels[s]);
                lv_obj_set_style_bg_color(cb->btn, cb->state_colors[s], 0);
                lv_unlock();
                break;
            }
        }
    }
}
