/*
 * sc_ui_screen_console.c — Game/media controller MFD layout renderer
 *
 * Reads all consoles from the active ship JSON, builds a swipeable tileview
 * where each tile is one console's grid of typed widgets (buttons, axes,
 * sliders, knobs, jog wheels) backed by the PSRAM sprite atlas.
 *
 * All HID output is routed through sc_hid_action_send().
 * Button states update in response to sc_gamelink events.
 */

#include "sc_ui_screen_console.h"
#include "sc_ui_theme.h"
#include "sc_ui_sprites.h"
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
static lv_obj_t *s_tab_bar = NULL;

/* Max buttons total across all consoles */
#define MAX_BUTTONS (128)
#define MAX_CONSOLES (8)

#include "sc_ui_theme.h"

static console_btn_t s_buttons[MAX_BUTTONS];
static int s_btn_count = 0;

typedef struct {
    char title[64];
    int grid_cols;
    int grid_rows;
} console_layout_t;

static console_layout_t s_consoles[MAX_CONSOLES];
static int s_console_count = 0;
static int s_initial_tile_idx = 0;

static lv_coord_t s_grid_col_dscs[MAX_CONSOLES][16];
static lv_coord_t s_grid_row_dscs[MAX_CONSOLES][16];

/* ── Button event callback ───────────────────────────────────────────────── */

static void btn_released_cb(lv_event_t *e)
{
    console_btn_t *cb = (console_btn_t *)lv_event_get_user_data(e);
    if (!cb) return;
    ESP_LOGD(TAG, "Widget action: %s", cb->action_id);

    /* Handle latching toggle state flip */
    if (cb->widget_type == SC_WIDGET_BTN_LATCHING && cb->widget) {
        cb->latching_state = !cb->latching_state;
        lv_lock();
        sc_ui_theme_style_btn_latching(cb->widget, cb->latching_state);
        lv_unlock();
    }

    esp_err_t ret = sc_hid_action_send(cb->action_id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HID send failed: %s", esp_err_to_name(ret));
    }
}

/** Parse widget_type string from JSON, default to BTN_MOMENTARY. */
static sc_widget_type_t parse_widget_type(const char *s)
{
    if (!s)                          return SC_WIDGET_BTN_MOMENTARY;
    if (strcmp(s, "btn_latching")    == 0) return SC_WIDGET_BTN_LATCHING;
    if (strcmp(s, "btn_danger")      == 0) return SC_WIDGET_BTN_DANGER;
    if (strcmp(s, "slider_h")        == 0) return SC_WIDGET_SLIDER_H;
    if (strcmp(s, "slider_v")        == 0) return SC_WIDGET_SLIDER_V;
    if (strcmp(s, "axis_joystick")   == 0) return SC_WIDGET_AXIS_JOYSTICK;
    if (strcmp(s, "axis_dpad")       == 0) return SC_WIDGET_AXIS_DPAD;
    if (strcmp(s, "axis_haat")       == 0) return SC_WIDGET_AXIS_HAAT;
    if (strcmp(s, "axis_throttle")   == 0) return SC_WIDGET_AXIS_THROTTLE;
    if (strcmp(s, "axis_yaw")        == 0) return SC_WIDGET_AXIS_YAW;
    if (strcmp(s, "axis_rudder")     == 0) return SC_WIDGET_AXIS_RUDDER;
    if (strcmp(s, "knob")            == 0) return SC_WIDGET_KNOB;
    if (strcmp(s, "jog_wheel")       == 0) return SC_WIDGET_JOG_WHEEL;
    return SC_WIDGET_BTN_MOMENTARY;
}

/** Create the correct LVGL widget tree for a given type inside a grid cell. */
static lv_obj_t *create_widget(lv_obj_t *grid, console_btn_t *cb)
{
    lv_obj_t *w = NULL;
    switch (cb->widget_type) {
        case SC_WIDGET_BTN_LATCHING:
            w = lv_button_create(grid);
            lv_obj_set_user_data(w, cb);
            sc_ui_theme_style_btn_latching(w, cb->latching_state);
            lv_obj_add_event_cb(w, btn_released_cb, LV_EVENT_RELEASED, cb);
            break;
        case SC_WIDGET_BTN_DANGER:
            w = lv_button_create(grid);
            lv_obj_set_user_data(w, cb);
            sc_ui_theme_style_btn(w, SC_COL_ARMED);
            lv_obj_add_event_cb(w, btn_released_cb, LV_EVENT_RELEASED, cb);
            break;
        case SC_WIDGET_SLIDER_H:
            w = sc_ui_theme_draw_slider_h(grid, cb);
            break;
        case SC_WIDGET_SLIDER_V:
            w = sc_ui_theme_draw_slider_v(grid, cb);
            break;
        case SC_WIDGET_AXIS_JOYSTICK:
            w = sc_ui_theme_draw_axis_joystick(grid, cb);
            break;
        case SC_WIDGET_AXIS_DPAD:
            w = sc_ui_theme_draw_axis_dpad(grid, cb);
            lv_obj_add_event_cb(w, btn_released_cb, LV_EVENT_RELEASED, cb);
            break;
        case SC_WIDGET_AXIS_HAAT:
            w = sc_ui_theme_draw_axis_haat(grid, cb);
            break;
        case SC_WIDGET_AXIS_THROTTLE:
            w = sc_ui_theme_draw_axis_throttle(grid, cb);
            break;
        case SC_WIDGET_AXIS_YAW:
            w = sc_ui_theme_draw_axis_yaw(grid, cb);
            break;
        case SC_WIDGET_AXIS_RUDDER:
            w = sc_ui_theme_draw_axis_rudder(grid, cb);
            break;
        case SC_WIDGET_KNOB:
            w = sc_ui_theme_draw_knob(grid, cb);
            break;
        case SC_WIDGET_JOG_WHEEL:
            w = sc_ui_theme_draw_jog_wheel(grid, cb);
            lv_obj_add_event_cb(w, btn_released_cb, LV_EVENT_RELEASED, cb);
            break;
        case SC_WIDGET_BTN_MOMENTARY:
        default:
            w = lv_button_create(grid);
            lv_obj_set_user_data(w, cb);
            sc_ui_theme_style_btn(w, SC_COL_BG_PANEL);
            lv_obj_add_event_cb(w, btn_released_cb, LV_EVENT_RELEASED, cb);
            break;
    }
    return w;
}

static void settings_btn_cb(lv_event_t *e)
{
    sc_ui_router_push(SC_UI_SCREEN_SETTINGS);
}

static void tab_btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_set_tile_id(s_tileview, idx, 0, LV_ANIM_ON);
}

static void tileview_scroll_cb(lv_event_t *e)
{
    if (!s_tab_bar) return;
    
    lv_obj_t *active_tile = lv_tileview_get_tile_active(s_tileview);
    if (!active_tile) return;
    
    int active_idx = lv_obj_get_index(active_tile);

    /* Update tab styles */
    for (int i = 0; i < s_console_count; i++) {
        lv_obj_t *tab = lv_obj_get_child(s_tab_bar, i);
        if (tab) {
            sc_ui_theme_style_tab(tab, (i == active_idx));
        }
    }
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

    if (s_root) {
        lv_obj_delete(s_root);
        s_root = NULL;
    }

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, SC_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    lv_obj_t *content = sc_ui_theme_draw_panel(s_root);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

    s_tileview = lv_tileview_create(content);
    lv_obj_set_width(s_tileview, LV_PCT(100));
    lv_obj_set_flex_grow(s_tileview, 1);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_tileview, tileview_scroll_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Populate tiles from loaded consoles */
    for (int i = 0; i < s_console_count; i++)
    {
        lv_obj_t *tile = lv_tileview_add_tile(s_tileview, i, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
        lv_obj_set_style_pad_all(tile, SC_UI_PAD_SMALL, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        /* Button grid */
        lv_obj_t *grid = lv_obj_create(tile);
        lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_pad_all(grid, SC_UI_PAD_SMALL, 0);
        lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_layout(grid, LV_LAYOUT_GRID);

        int cols = s_consoles[i].grid_cols;
        int rows = s_consoles[i].grid_rows;
        if (cols <= 0) cols = 4;
        if (rows <= 0) rows = 5;

        for(int c=0; c < cols && c < 15; c++) s_grid_col_dscs[i][c] = LV_GRID_FR(1);
        s_grid_col_dscs[i][cols] = LV_GRID_TEMPLATE_LAST;
        
        for(int r=0; r < rows && r < 15; r++) s_grid_row_dscs[i][r] = LV_GRID_FR(1);
        s_grid_row_dscs[i][rows] = LV_GRID_TEMPLATE_LAST;

        lv_obj_set_grid_dsc_array(grid, s_grid_col_dscs[i], s_grid_row_dscs[i]);

        /* Add widgets belonging to this console */
        for (int b = 0; b < s_btn_count; b++)
        {
            console_btn_t *cb = &s_buttons[b];
            if (cb->console_idx != i) continue;

            lv_obj_t *w = create_widget(grid, cb);
            if (!w) continue;

            lv_obj_set_grid_cell(w, LV_GRID_ALIGN_STRETCH, cb->col, cb->width,
                                     LV_GRID_ALIGN_STRETCH, cb->row, cb->height);
            cb->widget = w;

            /* Add text label for button types only */
            if (cb->widget_type == SC_WIDGET_BTN_MOMENTARY ||
                cb->widget_type == SC_WIDGET_BTN_LATCHING ||
                cb->widget_type == SC_WIDGET_BTN_DANGER)
            {
                lv_obj_t *lbl = lv_label_create(w);
                lv_label_set_text(lbl, cb->label_text[0] ? cb->label_text : cb->action_id);
                lv_obj_set_style_text_color(lbl, SC_COL_TEXT, 0);
                lv_obj_set_style_text_font(lbl, SC_FONT_MEDIUM, 0);
                lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
                cb->label = lbl;
            }
        }
    }

    /* Tab bar */
    s_tab_bar = lv_obj_create(content);
    lv_obj_set_size(s_tab_bar, LV_PCT(100), 100);
    lv_obj_set_style_bg_color(s_tab_bar, SC_COL_BG_PANEL, 0);
    lv_obj_set_style_border_color(s_tab_bar, SC_COL_DIVIDER, 0);
    lv_obj_set_style_border_width(s_tab_bar, 2, 0);
    lv_obj_set_style_border_side(s_tab_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_all(s_tab_bar, SC_UI_PAD_SMALL, 0);
    lv_obj_set_style_pad_gap(s_tab_bar, SC_UI_PAD_SMALL, 0);
    lv_obj_set_flex_flow(s_tab_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_tab_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(s_tab_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Create tab buttons */
    for (int i = 0; i < s_console_count; i++) {
        lv_obj_t *tab = lv_button_create(s_tab_bar);
        lv_obj_set_height(tab, LV_PCT(100));
        lv_obj_set_flex_grow(tab, 1);
        lv_obj_add_event_cb(tab, tab_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        
        lv_obj_t *lbl = lv_label_create(tab);
        lv_label_set_text(lbl, s_consoles[i].title);
        lv_obj_set_style_text_font(lbl, SC_FONT_MEDIUM, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        
        sc_ui_theme_style_tab(tab, (i == s_initial_tile_idx));
    }

    /* Settings Button at far right */
    lv_obj_t *settings_btn = lv_button_create(s_tab_bar);
    lv_obj_set_size(settings_btn, 80, LV_PCT(100));
    sc_ui_theme_style_tab(settings_btn, false);
    lv_obj_add_event_cb(settings_btn, settings_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *settings_lbl = lv_label_create(settings_btn);
    lv_label_set_text(settings_lbl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(settings_lbl, SC_FONT_TITLE, 0);
    lv_obj_align(settings_lbl, LV_ALIGN_CENTER, 0, 0);

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

    cJSON *manuf = cJSON_GetObjectItem(root, "manufacturer");
    if (manuf && cJSON_IsString(manuf)) {
        char manuf_lower[64] = {0};
        for (int i = 0; manuf->valuestring[i] && i < sizeof(manuf_lower) - 1; i++) {
            char c = manuf->valuestring[i];
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
            manuf_lower[i] = c;
        }
        if (strstr(manuf_lower, "drake")) {
            sc_ui_theme_init_drake_military();
        } else if (strstr(manuf_lower, "origin")) {
            sc_ui_theme_init_origin_lux();
        } else {
            sc_ui_theme_init_drake_military();
        }
    } else {
        sc_ui_theme_init_drake_military();
    }

    memset(s_buttons, 0, sizeof(s_buttons));
    memset(s_consoles, 0, sizeof(s_consoles));
    s_btn_count = 0;
    s_console_count = 0;
    s_initial_tile_idx = 0;

    static sc_hid_action_t hid_actions[SC_HID_MAX_ACTIONS];
    int hid_count = 0;

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
            strlcpy(s_consoles[s_console_count].title, disp_name->valuestring, sizeof(s_consoles[0].title));
        } else {
            strlcpy(s_consoles[s_console_count].title, "Console", sizeof(s_consoles[0].title));
        }

        cJSON *layout_obj = cJSON_GetObjectItem(c, "layout");
        if (cJSON_IsString(layout_obj) && strncmp(layout_obj->valuestring, "grid_", 5) == 0) {
            sscanf(layout_obj->valuestring, "grid_%dx%d", &s_consoles[s_console_count].grid_cols, &s_consoles[s_console_count].grid_rows);
        } else {
            s_consoles[s_console_count].grid_cols = 4;
            s_consoles[s_console_count].grid_rows = 5;
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

            cJSON *row_json = cJSON_GetObjectItem(action, "row");
            cJSON *col_json = cJSON_GetObjectItem(action, "col");
            cJSON *w_json = cJSON_GetObjectItem(action, "width");
            cJSON *h_json = cJSON_GetObjectItem(action, "height");

            cb->row = cJSON_IsNumber(row_json) ? row_json->valueint : 0;
            cb->col = cJSON_IsNumber(col_json) ? col_json->valueint : 0;
            cb->width = cJSON_IsNumber(w_json) ? w_json->valueint : 1;
            cb->height = cJSON_IsNumber(h_json) ? h_json->valueint : 1;

            cJSON *id      = cJSON_GetObjectItem(action, "id");
            cJSON *label   = cJSON_GetObjectItem(action, "label");
            cJSON *state   = cJSON_GetObjectItem(action, "state");
            cJSON *wtype_j = cJSON_GetObjectItem(action, "widget_type");
            cb->widget_type   = parse_widget_type(
                cJSON_IsString(wtype_j) ? wtype_j->valuestring : NULL);
            cb->latching_state = false;

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
                strlcpy(cb->label_text, label->valuestring, sizeof(cb->label_text));
            }

            /* Parse gamelink state config */
            if (cJSON_IsObject(state))
            {
                cJSON *evt_item = cJSON_GetObjectItem(state, "gamelink_event");
                if (cJSON_IsString(evt_item))
                {
                    strlcpy(cb->state_event, evt_item->valuestring,
                            sizeof(cb->state_event));
                    
                    /* Register gamelink handler for this event if not already registered for this console load */
                    bool already_registered = false;
                    for (int h = 0; h < s_btn_count; h++) {
                        if (strcmp(s_buttons[h].state_event, cb->state_event) == 0) {
                            already_registered = true;
                            break;
                        }
                    }
                    if (!already_registered) {
                        sc_gamelink_handler_register(cb->state_event,
                                                     (sc_gamelink_handler_fn_t)sc_ui_screen_console_on_event,
                                                     NULL);
                    }
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
                cJSON *gamepad = cJSON_GetObjectItem(hid, "gamepad_button");
                cJSON *cons = cJSON_GetObjectItem(hid, "consumer_usage");
                cJSON *hold = cJSON_GetObjectItem(hid, "hold_ms");
                if (cJSON_IsNumber(gamepad))
                    ha.gamepad_button = (uint8_t)gamepad->valueint;
                if (cJSON_IsNumber(cons))
                    ha.consumer_usage = (uint16_t)cons->valueint;
                if (cJSON_IsNumber(hold))
                    ha.hold_ms = (uint32_t)hold->valueint;
                if (hid_count < SC_HID_MAX_ACTIONS) {
                    hid_actions[hid_count++] = ha;
                }
            }

            s_btn_count++;
        }
        s_console_count++;
    }

    if (hid_count > 0) {
        sc_hid_action_table_load(hid_actions, hid_count);
    }

    sc_ui_sprite_rect_t r_tl = {0};
    sc_ui_sprites_get_rect(SC_SPRITE_PANEL_TL, &r_tl);
    int pad_x = r_tl.w > 0 ? r_tl.w : 16;
    int pad_y = r_tl.h > 0 ? r_tl.h : 16;

    int display_width = lv_display_get_horizontal_resolution(NULL);
    int display_height = lv_display_get_vertical_resolution(NULL);
    int total_w = display_width - 2 * pad_x - 32;
    int total_h = display_height - 2 * pad_y - 100 - 32;

    for (int b = 0; b < s_btn_count; b++) {
        console_btn_t *cb = &s_buttons[b];
        int cols = s_consoles[cb->console_idx].grid_cols;
        int rows = s_consoles[cb->console_idx].grid_rows;
        if (cols <= 0) cols = 4;
        if (rows <= 0) rows = 5;

        int cell_w = total_w / cols;
        int cell_h = total_h / rows;

        cb->pixel_w = cell_w * cb->width;
        cb->pixel_h = cell_h * cb->height;
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
        s_root    = NULL;
        s_tileview = NULL;
        s_tab_bar  = NULL;
    }
    lv_unlock();
    /* Clear widget pointers so stale refs can't be used */
    for (int i = 0; i < s_btn_count; i++) {
        s_buttons[i].widget = NULL;
        s_buttons[i].label  = NULL;
    }
    s_btn_count     = 0;
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
            if (strcmp(cb->state_keys[s], state_val) != 0) continue;
            lv_lock();
            if (cb->label)
                lv_label_set_text(cb->label, cb->state_labels[s]);
            if (cb->widget) {
                if (cb->widget_type == SC_WIDGET_BTN_LATCHING) {
                    /* Map state string to ON/OFF for latching buttons */
                    bool on = (strcmp(cb->state_keys[s], "on")  == 0 ||
                               strcmp(cb->state_keys[s], "up")  == 0 ||
                               strcmp(cb->state_keys[s], "active") == 0);
                    sc_ui_theme_style_btn_latching(cb->widget, on);
                } else {
                    sc_ui_theme_style_btn(cb->widget, cb->state_colors[s]);
                }
            }
            lv_unlock();
            break;
        }
    }
}
