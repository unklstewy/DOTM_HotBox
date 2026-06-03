/*
 * sc_ui_screen_console.c — Ship console MFD layout renderer
 *
 * Reads all consoles from the active ship JSON (loaded by sc_config), builds
 * a swipeable tileview where each tile is one console's grid of touch buttons,
 * and wires each button to sc_hid_action_send().
 *
 * Button states update in response to sc_gamelink events.
 */

#include "sc_ui_screen_console.h"
#include "sc_ui_theme.h"
#include "sc_config.h"
#include "sc_hid.h"
#include "sc_gamelink.h"
#include "sc_ui.h"

#include "cJSON.h"
#include "esp_log.h"

#include <string.h>

#include "lvgl.h"

static const char *TAG = "sc_ui_console";

/* ── State ───────────────────────────────────────────────────────────────── */
static lv_obj_t *s_root = NULL;
static lv_obj_t *s_tileview = NULL;

/* Max buttons total across all consoles */
#define MAX_BUTTONS (128)
#define MAX_CONSOLES (8)

typedef struct
{
    char action_id[32];
    lv_obj_t *btn;
    lv_obj_t *label;
    int console_idx;
    char state_event[48]; /* gamelink event name, or ""  */
    /* State colours from JSON */
    char state_keys[4][16]; /* e.g. "up", "down"           */
    lv_color_t state_colors[4];
    char state_labels[4][16];
    uint8_t state_count;
} console_btn_t;

static console_btn_t s_buttons[MAX_BUTTONS];
static int s_btn_count = 0;

static char s_console_titles[MAX_CONSOLES][64];
static int s_console_count = 0;
static int s_initial_tile_idx = 0;

/* ── Button event callback ───────────────────────────────────────────────── */

static void btn_released_cb(lv_event_t *e)
{
    const console_btn_t *btn = (const console_btn_t *)lv_event_get_user_data(e);
    if (!btn)
        return;
    ESP_LOGD(TAG, "Button pressed: %s", btn->action_id);
    esp_err_t ret = sc_hid_action_send(btn->action_id);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "HID send failed: %s", esp_err_to_name(ret));
    }
}

static void settings_btn_cb(lv_event_t *e)
{
    sc_ui_router_push(SC_UI_SCREEN_SETTINGS);
}

/* ── Parse colour from JSON hex string ────────────────────────────────────── */

static lv_color_t parse_hex_color(const char *hex)
{
    if (!hex || hex[0] != '#')
        return SC_COL_OFF;
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
    lv_obj_set_style_pad_all(s_root, 0, 0); /* Tileview will fill it */
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_tileview = lv_tileview_create(s_root);
    lv_obj_set_size(s_tileview, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, 0);

    /* Populate tiles from loaded consoles */
    for (int i = 0; i < s_console_count; i++)
    {
        lv_obj_t *tile = lv_tileview_add_tile(s_tileview, i, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
        lv_obj_set_style_pad_all(tile, SC_UI_PAD_SMALL, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        /* Title bar for this console */
        lv_obj_t *title_bar = lv_obj_create(tile);
        lv_obj_set_width(title_bar, LV_PCT(100));
        lv_obj_set_style_bg_opa(title_bar, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(title_bar, 0, 0);
        lv_obj_set_style_pad_all(title_bar, 0, 0);
        lv_obj_set_style_pad_bottom(title_bar, SC_UI_PAD_MEDIUM, 0);
        lv_obj_set_layout(title_bar, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(title_bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(title_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *title = lv_label_create(title_bar);
        lv_label_set_text(title, s_console_titles[i]);
        lv_obj_set_style_text_color(title, SC_COL_ACCENT, 0);
        lv_obj_set_style_text_font(title, SC_FONT_TITLE, 0);

        lv_obj_t *settings_btn = lv_button_create(title_bar);
        lv_obj_set_size(settings_btn, 60, 60);
        lv_obj_set_style_bg_color(settings_btn, SC_COL_BG_PANEL, 0);
        lv_obj_set_style_radius(settings_btn, SC_UI_BTN_RADIUS, 0);
        lv_obj_add_event_cb(settings_btn, settings_btn_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *settings_lbl = lv_label_create(settings_btn);
        lv_label_set_text(settings_lbl, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_color(settings_lbl, SC_COL_TEXT, 0);
        lv_obj_align(settings_lbl, LV_ALIGN_CENTER, 0, 0);

        /* Button grid */
        lv_obj_t *grid = lv_obj_create(tile);
        lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_pad_all(grid, SC_UI_PAD_SMALL, 0);
        lv_obj_set_style_pad_gap(grid, SC_UI_PAD_SMALL, 0);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

        /* Add buttons belonging to this console */
        for (int b = 0; b < s_btn_count; b++)
        {
            console_btn_t *cb = &s_buttons[b];
            if (cb->console_idx != i) continue;

            lv_obj_t *btn = lv_button_create(grid);
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

            cb->btn = btn;
            cb->label = lbl;
        }
    }

    if (s_console_count > 0 && s_initial_tile_idx < s_console_count) {
        lv_obj_set_tile_id(s_tileview, s_initial_tile_idx, 0, LV_ANIM_OFF);
    }

    lv_unlock();
    return s_root;
}

/* ── Load console from config ────────────────────────────────────────────── */

void sc_ui_screen_console_load(const sc_terminal_config_t *cfg)
{
    ESP_LOGI(TAG, "Loading all consoles for ship: %s (initial: %s)", cfg->ship_id, cfg->console_id);

    /* Load ship JSON */
    char *json_str = NULL;
    size_t json_len = 0;
    if (sc_config_ship_json_load(&json_str, &json_len) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load ship JSON");
        return;
    }

    cJSON *root = cJSON_ParseWithLength(json_str, json_len);
    free(json_str);
    if (!root)
    {
        ESP_LOGE(TAG, "JSON parse error");
        return;
    }

    s_btn_count = 0;
    s_console_count = 0;
    s_initial_tile_idx = 0;

    cJSON *consoles = cJSON_GetObjectItem(root, "consoles");
    cJSON *c = NULL;
    cJSON_ArrayForEach(c, consoles)
    {
        if (s_console_count >= MAX_CONSOLES) break;

        cJSON *cid = cJSON_GetObjectItem(c, "console_id");
        if (cJSON_IsString(cid) && strcmp(cid->valuestring, cfg->console_id) == 0) {
            s_initial_tile_idx = s_console_count;
        }

        cJSON *disp_name = cJSON_GetObjectItem(c, "display_name");
        if (cJSON_IsString(disp_name)) {
            strlcpy(s_console_titles[s_console_count], disp_name->valuestring, sizeof(s_console_titles[0]));
        } else {
            strlcpy(s_console_titles[s_console_count], "Console", sizeof(s_console_titles[0]));
        }

        cJSON *actions = cJSON_GetObjectItem(c, "actions");
        cJSON *action = NULL;
        cJSON_ArrayForEach(action, actions)
        {
            if (s_btn_count >= MAX_BUTTONS)
                break;
            console_btn_t *cb = &s_buttons[s_btn_count];
            memset(cb, 0, sizeof(*cb));
            cb->console_idx = s_console_count;

            cJSON *id = cJSON_GetObjectItem(action, "id");
            cJSON *label = cJSON_GetObjectItem(action, "label");
            cJSON *state = cJSON_GetObjectItem(action, "state");

            if (!cJSON_IsString(id))
                continue;
            strlcpy(cb->action_id, id->valuestring, sizeof(cb->action_id));

            /* If recreating, the widget doesn't exist yet so label update will happen in create() */
            if (cb->label && cJSON_IsString(label))
            {
                lv_lock();
                lv_label_set_text(cb->label, label->valuestring);
                lv_unlock();
            } else if (!cb->label && cJSON_IsString(label)) {
                /* Temporarily store in action_id so create() uses it */
                strlcpy(cb->action_id, label->valuestring, sizeof(cb->action_id));
            }

            /* Parse gamelink state config */
            if (cJSON_IsObject(state))
            {
                cJSON *evt_item = cJSON_GetObjectItem(state, "gamelink_event");
                if (cJSON_IsString(evt_item))
                {
                    strlcpy(cb->state_event, evt_item->valuestring,
                            sizeof(cb->state_event));
                    /* Register gamelink handler for this button */
                    sc_gamelink_handler_register(cb->state_event,
                                                 (sc_gamelink_handler_fn_t)sc_ui_screen_console_on_event,
                                                 NULL);
                }
                cJSON *values = cJSON_GetObjectItem(state, "values");
                if (cJSON_IsObject(values))
                {
                    cJSON *v = NULL;
                    cJSON_ArrayForEach(v, values)
                    {
                        if (cb->state_count >= 4)
                            break;
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
            if (cJSON_IsObject(hid))
            {
                sc_hid_action_t ha = {0};
                /* Restore the real action id for HID lookup */
                strlcpy(ha.action_id, id->valuestring, sizeof(ha.action_id));
                cJSON *mod = cJSON_GetObjectItem(hid, "modifier");
                cJSON *keys = cJSON_GetObjectItem(hid, "keycodes");
                cJSON *cons = cJSON_GetObjectItem(hid, "consumer_usage");
                cJSON *hold = cJSON_GetObjectItem(hid, "hold_ms");
                if (cJSON_IsNumber(mod))
                    ha.modifier = (uint8_t)mod->valueint;
                if (cJSON_IsNumber(cons))
                    ha.consumer_usage = (uint16_t)cons->valueint;
                if (cJSON_IsNumber(hold))
                    ha.hold_ms = (uint32_t)hold->valueint;
                if (cJSON_IsArray(keys))
                {
                    int ki = 0;
                    cJSON *k = NULL;
                    cJSON_ArrayForEach(k, keys)
                    {
                        if (ki >= 6)
                            break;
                        ha.keycodes[ki++] = (uint8_t)k->valueint;
                    }
                }
                sc_hid_action_table_load(&ha, 1);
            }

            s_btn_count++;
        }
        s_console_count++;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %d consoles, %d buttons total", s_console_count, s_btn_count);
}

/* ── Destroy ─────────────────────────────────────────────────────────────── */

void sc_ui_screen_console_destroy(void)
{
    lv_lock();
    if (s_root)
    {
        lv_obj_delete(s_root);
        s_root = NULL;
        s_tileview = NULL;
    }
    lv_unlock();
    s_btn_count = 0;
    s_console_count = 0;
}

/* ── Gamelink event handler ──────────────────────────────────────────────── */

void sc_ui_screen_console_on_event(const sc_gamelink_event_t *evt,
                                   void *user_ctx)
{
    (void)user_ctx;

    if (!s_root || !evt)
        return;

    /* Find buttons that care about this event */
    for (int i = 0; i < s_btn_count; i++)
    {
        console_btn_t *cb = &s_buttons[i];
        if (strcmp(cb->state_event, evt->event_id) != 0)
            continue;

        /* Determine new state from payload — use raw for now */
        const char *state_val = evt->payload.raw;
        /* For typed events, use the named fields: */
        if (strcmp(evt->event_id, SC_GAMELINK_EVT_GEAR_STATE) == 0)
        {
            state_val = evt->payload.gear.state;
        }

        /* Find matching state entry */
        for (int s = 0; s < cb->state_count; s++)
        {
            if (strcmp(cb->state_keys[s], state_val) == 0)
            {
                lv_lock();
                lv_label_set_text(cb->label, cb->state_labels[s]);
                lv_obj_set_style_bg_color(cb->btn, cb->state_colors[s], 0);
                lv_unlock();
                break;
            }
        }
    }
}
