/*
 * sc_ui_screen_settings.c — Terminal settings screen
 *
 * Implements a modern multi-tab sidebar layout comparable to the Web UI:
 *   - Status tab: live telemetry, system memory, and network IP.
 *   - Target tab: dropdowns for active ship/console, terminal index, and display orientation.
 *   - Wi-Fi tab: scan networks, enter password, and join Wi-Fi.
 *   - Hardware tab: backlight slider, touch calibration, theme selector, factory reset.
 */

#include "sc_ui_screen_settings.h"
#include "sc_ui_theme.h"
#include "sc_config.h"
#include "sc_ui_screens.h"
#include "sc_network.h"
#include "sc_ui.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "dirent.h"
#include "sys/stat.h"
#include "unistd.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sc_ui_settings";

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_sidebar = NULL;
static lv_obj_t *s_content_area = NULL;
static lv_obj_t *s_content_inner = NULL;
static lv_obj_t *s_nav_buttons[4] = {NULL};

typedef enum {
    TAB_STATUS = 0,
    TAB_TARGET,
    TAB_WIFI,
    TAB_HARDWARE,
    TAB_COUNT
} settings_tab_t;

static settings_tab_t s_active_tab = TAB_STATUS;

/* ── Status Tab State ── */
static lv_timer_t *s_status_timer = NULL;
static lv_obj_t *s_status_fields[6] = {NULL}; // GameLink, IP, Mode, SSID, Heap, Uptime

/* ── Target Tab State ── */
#define MAX_SHIPS 16
static struct {
    char id[32];
    char name[64];
} s_ships[MAX_SHIPS];
static int s_ship_count = 0;

#define MAX_CONSOLES 8
static struct {
    char id[32];
    char name[64];
} s_consoles[MAX_CONSOLES];
static int s_console_count = 0;

static lv_obj_t *s_ship_dropdown = NULL;
static lv_obj_t *s_console_dropdown = NULL;
static lv_obj_t *s_index_val_lbl = NULL;
static lv_obj_t *s_rot_dropdown = NULL;

/* ── Wi-Fi Tab State ── */
static lv_obj_t *s_wifi_list = NULL;
static lv_obj_t *s_wifi_status_lbl = NULL;
static char s_wifi_scan_buf[4096];
static char s_connecting_ssid[64] = "";
static lv_obj_t *s_wifi_modal = NULL;
static lv_obj_t *s_wifi_password_ta = NULL;
static lv_obj_t *s_wifi_kb = NULL;

/* ── Hardware Tab State ── */
static lv_obj_t *s_brightness_val_lbl = NULL;

/* Forward declarations */
static void draw_active_tab_content(void);
static void draw_status_tab(void);
static void draw_target_tab(void);
static void draw_wifi_tab(void);
static void draw_hardware_tab(void);

/* ── NVS Helpers ── */
static void get_nvs_ssid(char *buf, size_t max_len)
{
    nvs_handle_t h;
    if (nvs_open("sc_config", NVS_READONLY, &h) == ESP_OK) {
        size_t len = max_len;
        if (nvs_get_str(h, "ssid", buf, &len) != ESP_OK) {
            strlcpy(buf, "None", max_len);
        }
        nvs_close(h);
    } else {
        strlcpy(buf, "None", max_len);
    }
}

static uint8_t get_nvs_brightness(void)
{
    nvs_handle_t h;
    uint8_t val = 80;
    if (nvs_open("sc_config", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "brightness", &val);
        nvs_close(h);
    }
    return val;
}

static void save_nvs_brightness(uint8_t val)
{
    nvs_handle_t h;
    if (nvs_open("sc_config", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "brightness", val);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ── Navigation / Back Callback ── */
static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    sc_ui_router_pop();
}

static void nav_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    for (int i = 0; i < TAB_COUNT; i++) {
        if (s_nav_buttons[i] == btn) {
            if (s_active_tab != (settings_tab_t)i) {
                s_active_tab = (settings_tab_t)i;
                draw_active_tab_content();
            }
            break;
        }
    }
}

/* ── Ships & Consoles Directory Scanning ── */
static void scan_ships_dir(void)
{
    s_ship_count = 0;
    DIR *dir = opendir("/sdcard/ships");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open ships directory");
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_ship_count < MAX_SHIPS) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        size_t len = strlen(entry->d_name);
        if (len < 6 || strcmp(entry->d_name + len - 5, ".json") != 0) {
            continue;
        }
        if (strcmp(entry->d_name, "ship_templates.json") == 0) {
            continue;
        }

        char filepath[128];
        snprintf(filepath, sizeof(filepath), "/sdcard/ships/%s", entry->d_name);
        FILE *f = fopen(filepath, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            size_t sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *buf = malloc(sz + 1);
            if (buf) {
                fread(buf, 1, sz, f);
                buf[sz] = '\0';
                cJSON *root = cJSON_Parse(buf);
                if (root) {
                    cJSON *s_id = cJSON_GetObjectItem(root, "ship_id");
                    cJSON *s_name = cJSON_GetObjectItem(root, "ship_name");
                    if (s_id && s_id->valuestring && s_name && s_name->valuestring) {
                        strlcpy(s_ships[s_ship_count].id, s_id->valuestring, sizeof(s_ships[0].id));
                        strlcpy(s_ships[s_ship_count].name, s_name->valuestring, sizeof(s_ships[0].name));
                        s_ship_count++;
                    }
                    cJSON_Delete(root);
                }
                free(buf);
            }
            fclose(f);
        }
    }
    closedir(dir);
}

static void load_consoles_for_ship(const char *ship_id)
{
    s_console_count = 0;
    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/sdcard/ships/%s.json", ship_id);
    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open ship json: %s", filepath);
        return;
    }
    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse json: %s", filepath);
        return;
    }

    cJSON *consoles_arr = cJSON_GetObjectItem(root, "consoles");
    if (consoles_arr && cJSON_IsArray(consoles_arr)) {
        int arr_sz = cJSON_GetArraySize(consoles_arr);
        for (int i = 0; i < arr_sz && s_console_count < MAX_CONSOLES; i++) {
            cJSON *c_item = cJSON_GetArrayItem(consoles_arr, i);
            cJSON *c_id = cJSON_GetObjectItem(c_item, "console_id");
            cJSON *c_name = cJSON_GetObjectItem(c_item, "display_name");
            if (c_id && c_id->valuestring && c_name && c_name->valuestring) {
                strlcpy(s_consoles[s_console_count].id, c_id->valuestring, sizeof(s_consoles[0].id));
                strlcpy(s_consoles[s_console_count].name, c_name->valuestring, sizeof(s_consoles[0].name));
                s_console_count++;
            }
        }
    }
    cJSON_Delete(root);
}

/* ── Status Tab Callback ── */
static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    sc_network_state_t net_state = sc_network_state_get();
    const char *state_str = "Disconnected";
    switch (net_state) {
        case SC_NET_STATE_DISCONNECTED: state_str = "Disconnected"; break;
        case SC_NET_STATE_CONNECTING:   state_str = "Connecting..."; break;
        case SC_NET_STATE_WIFI_UP:      state_str = "Wi-Fi Connected"; break;
        case SC_NET_STATE_AP_UP:        state_str = "SoftAP Active"; break;
        case SC_NET_STATE_WS_CONNECTED: state_str = "GameLink Active (Online)"; break;
    }

    char ip_str[32] = "192.168.4.1";
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
    }

    char ssid[32];
    get_nvs_ssid(ssid, sizeof(ssid));
    bool is_ap = sc_network_is_ap();

    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    int64_t uptime_sec = esp_timer_get_time() / 1000000;
    int hrs = uptime_sec / 3600;
    int mins = (uptime_sec % 3600) / 60;
    int secs = uptime_sec % 60;

    char uptime_str[64];
    if (hrs > 0) {
        snprintf(uptime_str, sizeof(uptime_str), "%d hrs, %d mins, %d secs", hrs, mins, secs);
    } else {
        snprintf(uptime_str, sizeof(uptime_str), "%d mins, %d secs", mins, secs);
    }

    char heap_str[64];
    snprintf(heap_str, sizeof(heap_str), "Int: %lu KB | PSRAM: %.1f MB", 
             (unsigned long)(free_heap / 1024), (double)free_psram / (1024.0 * 1024.0));

    if (s_status_fields[0]) lv_label_set_text(s_status_fields[0], state_str);
    if (s_status_fields[1]) lv_label_set_text(s_status_fields[1], ip_str);
    if (s_status_fields[2]) lv_label_set_text(s_status_fields[2], is_ap ? "SoftAP" : "Station");
    if (s_status_fields[3]) lv_label_set_text(s_status_fields[3], is_ap ? "SC_Terminal" : ssid);
    if (s_status_fields[4]) lv_label_set_text(s_status_fields[4], heap_str);
    if (s_status_fields[5]) lv_label_set_text(s_status_fields[5], uptime_str);
}

static void draw_status_tab(void)
{
    lv_obj_t *title = lv_label_create(s_content_inner);
    lv_label_set_text(title, "System Telemetry & Status");
    lv_obj_set_style_text_font(title, SC_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(title, SC_COL_ACCENT, 0);

    const char *labels[] = {
        "System State:", "IP Address:", "Network Mode:", "Wi-Fi SSID:", "Free Memory:", "Uptime:"
    };

    for (int i = 0; i < 6; i++) {
        lv_obj_t *row = lv_obj_create(s_content_inner);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);

        lv_obj_t *lbl_k = lv_label_create(row);
        lv_label_set_text(lbl_k, labels[i]);
        lv_obj_set_width(lbl_k, 130);
        lv_obj_set_style_text_font(lbl_k, SC_FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl_k, SC_COL_TEXT_DIM, 0);

        s_status_fields[i] = lv_label_create(row);
        lv_label_set_text(s_status_fields[i], "Loading...");
        lv_obj_set_style_text_font(s_status_fields[i], SC_FONT_SMALL, 0);
        lv_obj_set_style_text_color(s_status_fields[i], SC_COL_TEXT, 0);
    }

    status_timer_cb(NULL);
    s_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
}

/* ── Target Tab Callbacks ── */
static void ship_dropdown_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint32_t sel = lv_dropdown_get_selected(dd);
    if (sel < s_ship_count) {
        sc_terminal_config_t cfg = *sc_config_get();
        strlcpy(cfg.ship_id, s_ships[sel].id, sizeof(cfg.ship_id));
        sc_config_save(&cfg);

        load_consoles_for_ship(s_ships[sel].id);
        
        if (s_console_dropdown) {
            lv_dropdown_clear_options(s_console_dropdown);
            for (int i = 0; i < s_console_count; i++) {
                lv_dropdown_add_option(s_console_dropdown, s_consoles[i].name, i);
            }
            if (s_console_count > 0) {
                lv_dropdown_set_selected(s_console_dropdown, 0);
                strlcpy(cfg.console_id, s_consoles[0].id, sizeof(cfg.console_id));
                sc_config_save(&cfg);
            }
        }
    }
}

static void console_dropdown_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint32_t sel = lv_dropdown_get_selected(dd);
    if (sel < s_console_count) {
        sc_terminal_config_t cfg = *sc_config_get();
        strlcpy(cfg.console_id, s_consoles[sel].id, sizeof(cfg.console_id));
        sc_config_save(&cfg);
    }
}

static void index_dec_cb(lv_event_t *e)
{
    (void)e;
    sc_terminal_config_t cfg = *sc_config_get();
    if (cfg.terminal_index > 0) {
        cfg.terminal_index--;
        sc_config_save(&cfg);
        if (s_index_val_lbl) {
            lv_label_set_text_fmt(s_index_val_lbl, "%d", cfg.terminal_index);
        }
    }
}

static void index_inc_cb(lv_event_t *e)
{
    (void)e;
    sc_terminal_config_t cfg = *sc_config_get();
    cfg.terminal_index++;
    sc_config_save(&cfg);
    if (s_index_val_lbl) {
        lv_label_set_text_fmt(s_index_val_lbl, "%d", cfg.terminal_index);
    }
}

static void orientation_dropdown_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint32_t sel = lv_dropdown_get_selected(dd);
    sc_terminal_config_t cfg = *sc_config_get();
    cfg.display_rotation = sel;
    sc_config_save(&cfg);
    sc_ui_set_rotation(sel);
}

static void apply_layout_btn_cb(lv_event_t *e)
{
    (void)e;
    sc_ui_router_home();
}

static void draw_target_tab(void)
{
    scan_ships_dir();
    const sc_terminal_config_t *cfg = sc_config_get();
    load_consoles_for_ship(cfg->ship_id);

    lv_obj_t *title = lv_label_create(s_content_inner);
    lv_label_set_text(title, "Active Ship Configuration");
    lv_obj_set_style_text_font(title, SC_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(title, SC_COL_ACCENT, 0);

    lv_obj_t *ship_lbl = lv_label_create(s_content_inner);
    lv_label_set_text(ship_lbl, "Active Ship Target");
    lv_obj_set_style_text_font(ship_lbl, SC_FONT_SMALL, 0);
    lv_obj_set_style_text_color(ship_lbl, SC_COL_TEXT_DIM, 0);

    s_ship_dropdown = lv_dropdown_create(s_content_inner);
    lv_obj_set_width(s_ship_dropdown, LV_PCT(90));
    lv_dropdown_clear_options(s_ship_dropdown);
    int active_ship_idx = 0;
    for (int i = 0; i < s_ship_count; i++) {
        lv_dropdown_add_option(s_ship_dropdown, s_ships[i].name, i);
        if (strcmp(s_ships[i].id, cfg->ship_id) == 0) {
            active_ship_idx = i;
        }
    }
    if (s_ship_count > 0) {
        lv_dropdown_set_selected(s_ship_dropdown, active_ship_idx);
    }
    lv_obj_add_event_cb(s_ship_dropdown, ship_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *console_lbl = lv_label_create(s_content_inner);
    lv_label_set_text(console_lbl, "Selected MFD Layout");
    lv_obj_set_style_text_font(console_lbl, SC_FONT_SMALL, 0);
    lv_obj_set_style_text_color(console_lbl, SC_COL_TEXT_DIM, 0);

    s_console_dropdown = lv_dropdown_create(s_content_inner);
    lv_obj_set_width(s_console_dropdown, LV_PCT(90));
    lv_dropdown_clear_options(s_console_dropdown);
    int active_console_idx = 0;
    for (int i = 0; i < s_console_count; i++) {
        lv_dropdown_add_option(s_console_dropdown, s_consoles[i].name, i);
        if (strcmp(s_consoles[i].id, cfg->console_id) == 0) {
            active_console_idx = i;
        }
    }
    if (s_console_count > 0) {
        lv_dropdown_set_selected(s_console_dropdown, active_console_idx);
    }
    lv_obj_add_event_cb(s_console_dropdown, console_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *index_lbl = lv_label_create(s_content_inner);
    lv_label_set_text(index_lbl, "Terminal Index");
    lv_obj_set_style_text_font(index_lbl, SC_FONT_SMALL, 0);
    lv_obj_set_style_text_color(index_lbl, SC_COL_TEXT_DIM, 0);

    lv_obj_t *index_row = lv_obj_create(s_content_inner);
    lv_obj_set_size(index_row, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(index_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(index_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(index_row, 0, 0);
    lv_obj_set_style_pad_all(index_row, 0, 0);
    lv_obj_set_style_pad_gap(index_row, 15, 0);

    lv_obj_t *dec_btn = lv_button_create(index_row);
    lv_obj_set_size(dec_btn, 40, 36);
    lv_obj_set_style_bg_color(dec_btn, SC_COL_ACCENT, 0);
    lv_obj_add_event_cb(dec_btn, index_dec_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *dec_lbl = lv_label_create(dec_btn);
    lv_label_set_text(dec_lbl, "-");
    lv_obj_center(dec_lbl);

    s_index_val_lbl = lv_label_create(index_row);
    lv_label_set_text_fmt(s_index_val_lbl, "%d", cfg->terminal_index);
    lv_obj_set_style_text_font(s_index_val_lbl, SC_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(s_index_val_lbl, SC_COL_TEXT, 0);

    lv_obj_t *inc_btn = lv_button_create(index_row);
    lv_obj_set_size(inc_btn, 40, 36);
    lv_obj_set_style_bg_color(inc_btn, SC_COL_ACCENT, 0);
    lv_obj_add_event_cb(inc_btn, index_inc_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *inc_lbl = lv_label_create(inc_btn);
    lv_label_set_text(inc_lbl, "+");
    lv_obj_center(inc_lbl);

    lv_obj_t *rot_lbl = lv_label_create(s_content_inner);
    lv_label_set_text(rot_lbl, "Display Orientation");
    lv_obj_set_style_text_font(rot_lbl, SC_FONT_SMALL, 0);
    lv_obj_set_style_text_color(rot_lbl, SC_COL_TEXT_DIM, 0);

    s_rot_dropdown = lv_dropdown_create(s_content_inner);
    lv_obj_set_width(s_rot_dropdown, LV_PCT(90));
    lv_dropdown_clear_options(s_rot_dropdown);
    lv_dropdown_add_option(s_rot_dropdown, "Portrait (0°)", 0);
    lv_dropdown_add_option(s_rot_dropdown, "Landscape (90°)", 1);
    lv_dropdown_add_option(s_rot_dropdown, "Portrait (180°)", 2);
    lv_dropdown_add_option(s_rot_dropdown, "Landscape (270°)", 3);
    lv_dropdown_set_selected(s_rot_dropdown, cfg->display_rotation);
    lv_obj_add_event_cb(s_rot_dropdown, orientation_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *apply_btn = lv_button_create(s_content_inner);
    lv_obj_set_size(apply_btn, LV_PCT(90), 44);
    lv_obj_set_style_bg_color(apply_btn, SC_COL_READY, 0);
    lv_obj_add_event_cb(apply_btn, apply_layout_btn_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *apply_lbl = lv_label_create(apply_btn);
    lv_label_set_text(apply_lbl, "APPLY & LOAD LAYOUT");
    lv_obj_set_style_text_color(apply_lbl, SC_COL_BG, 0);
    lv_obj_set_style_text_font(apply_lbl, SC_FONT_MEDIUM, 0);
    lv_obj_center(apply_lbl);
}

/* ── Wi-Fi Tab Callbacks ── */
static void wifi_connect_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (s_wifi_modal) {
        lv_obj_delete(s_wifi_modal);
        s_wifi_modal = NULL;
    }
}

static void wifi_connect_confirm_cb(lv_event_t *e)
{
    (void)e;
    if (s_wifi_password_ta && s_connecting_ssid[0] != '\0') {
        const char *password = lv_textarea_get_text(s_wifi_password_ta);
        ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s", s_connecting_ssid);
        sc_network_set_wifi_credentials(s_connecting_ssid, password);
    }
}

static void wifi_list_item_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = lv_list_get_button_text(s_wifi_list, btn);
    if (!ssid) return;

    strlcpy(s_connecting_ssid, ssid, sizeof(s_connecting_ssid));

    s_wifi_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_wifi_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_wifi_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_wifi_modal, LV_OPA_70, 0);
    lv_obj_set_flex_flow(s_wifi_modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_wifi_modal, 24, 0);
    lv_obj_set_style_pad_gap(s_wifi_modal, 16, 0);
    lv_obj_set_style_border_width(s_wifi_modal, 0, 0);

    lv_obj_t *card = lv_obj_create(s_wifi_modal);
    lv_obj_set_size(card, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(card, SC_COL_BG_PANEL, 0);
    lv_obj_set_style_border_color(card, SC_COL_ACCENT, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_gap(card, 12, 0);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text_fmt(title, "Connect to: %s", ssid);
    lv_obj_set_style_text_font(title, SC_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(title, SC_COL_TEXT, 0);

    s_wifi_password_ta = lv_textarea_create(card);
    lv_textarea_set_placeholder_text(s_wifi_password_ta, "Password");
    lv_textarea_set_password_mode(s_wifi_password_ta, true);
    lv_obj_set_width(s_wifi_password_ta, LV_PCT(100));

    s_wifi_kb = lv_keyboard_create(s_wifi_modal);
    lv_obj_set_size(s_wifi_kb, LV_PCT(100), LV_PCT(45));
    lv_keyboard_set_textarea(s_wifi_kb, s_wifi_password_ta);
    lv_obj_set_style_bg_color(s_wifi_kb, SC_COL_BG_PANEL, 0);
    
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 16, 0);

    lv_obj_t *cancel_btn = lv_button_create(row);
    lv_obj_set_style_bg_color(cancel_btn, SC_COL_ARMED, 0);
    lv_obj_add_event_cb(cancel_btn, wifi_connect_cancel_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");

    lv_obj_t *conn_btn = lv_button_create(row);
    lv_obj_set_style_bg_color(conn_btn, SC_COL_READY, 0);
    lv_obj_add_event_cb(conn_btn, wifi_connect_confirm_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *conn_lbl = lv_label_create(conn_btn);
    lv_label_set_text(conn_lbl, "Connect");
    lv_obj_set_style_text_color(conn_lbl, SC_COL_BG, 0);
}

static void wifi_scan_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_wifi_status_lbl) {
        lv_label_set_text(s_wifi_status_lbl, "Scanning networks...");
    }
    if (s_wifi_list) {
        lv_obj_clean(s_wifi_list);
    }

    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        lv_refr_now(disp);
    }

    esp_err_t err = sc_network_scan_wifi(s_wifi_scan_buf, sizeof(s_wifi_scan_buf));
    if (err != ESP_OK) {
        if (s_wifi_status_lbl) {
            lv_label_set_text_fmt(s_wifi_status_lbl, "Scan failed: 0x%X", err);
        }
        return;
    }

    cJSON *root = cJSON_Parse(s_wifi_scan_buf);
    if (!root) {
        if (s_wifi_status_lbl) {
            lv_label_set_text(s_wifi_status_lbl, "Scan parse error");
        }
        return;
    }

    int count = cJSON_GetArraySize(root);
    if (s_wifi_status_lbl) {
        lv_label_set_text_fmt(s_wifi_status_lbl, "Found %d networks:", count);
    }

    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        cJSON *ssid = cJSON_GetObjectItem(item, "ssid");
        cJSON *rssi = cJSON_GetObjectItem(item, "rssi");
        cJSON *secure = cJSON_GetObjectItem(item, "secure");
        if (ssid && ssid->valuestring && ssid->valuestring[0] != '\0') {
            int rssi_val = rssi ? rssi->valueint : -100;
            bool is_sec = secure ? secure->valueint : false;
            
            lv_obj_t *btn = lv_list_add_button(s_wifi_list, NULL, ssid->valuestring);
            lv_obj_set_style_bg_color(btn, SC_COL_BG_PANEL, 0);
            lv_obj_set_style_text_color(btn, SC_COL_TEXT, 0);
            lv_obj_set_style_border_color(btn, SC_COL_DIVIDER, 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_pad_all(btn, 10, 0);
            
            lv_obj_t *rssi_lbl = lv_label_create(btn);
            lv_label_set_text_fmt(rssi_lbl, "%d dBm %s", rssi_val, is_sec ? "🔒" : "");
            lv_obj_align(rssi_lbl, LV_ALIGN_RIGHT_MID, -10, 0);
            lv_obj_set_style_text_color(rssi_lbl, SC_COL_TEXT_DIM, 0);
            lv_obj_set_style_text_font(rssi_lbl, SC_FONT_SMALL, 0);

            lv_obj_add_event_cb(btn, wifi_list_item_cb, LV_EVENT_RELEASED, NULL);
        }
    }
    cJSON_Delete(root);
}

static void draw_wifi_tab(void)
{
    lv_obj_t *title = lv_label_create(s_content_inner);
    lv_label_set_text(title, "Wi-Fi Settings");
    lv_obj_set_style_text_font(title, SC_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(title, SC_COL_ACCENT, 0);

    lv_obj_t *scan_btn = lv_button_create(s_content_inner);
    lv_obj_set_size(scan_btn, LV_PCT(90), 40);
    lv_obj_set_style_bg_color(scan_btn, SC_COL_ACCENT, 0);
    lv_obj_add_event_cb(scan_btn, wifi_scan_btn_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, "SCAN NETWORKS");
    lv_obj_set_style_text_color(scan_lbl, SC_COL_BG, 0);
    lv_obj_set_style_text_font(scan_lbl, SC_FONT_MEDIUM, 0);
    lv_obj_center(scan_lbl);

    s_wifi_status_lbl = lv_label_create(s_content_inner);
    lv_label_set_text(s_wifi_status_lbl, "Click Scan to discover nearby networks");
    lv_obj_set_style_text_font(s_wifi_status_lbl, SC_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_wifi_status_lbl, SC_COL_TEXT_DIM, 0);

    s_wifi_list = lv_list_create(s_content_inner);
    lv_obj_set_size(s_wifi_list, LV_PCT(90), 400);
    lv_obj_set_style_bg_color(s_wifi_list, SC_COL_BG, 0);
    lv_obj_set_style_border_color(s_wifi_list, SC_COL_DIVIDER, 0);
    lv_obj_set_style_border_width(s_wifi_list, 1, 0);
    lv_obj_set_style_pad_all(s_wifi_list, 0, 0);
}

/* ── Hardware Tab Callbacks ── */
static void brightness_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    if (s_brightness_val_lbl) {
        lv_label_set_text_fmt(s_brightness_val_lbl, "%d%%", val);
    }
    sc_ui_brightness_set(val);
}

static void brightness_slider_released_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    save_nvs_brightness(val);
}

static void hw_calibrate_btn_cb(lv_event_t *e)
{
    (void)e;
    sc_ui_router_push(SC_UI_SCREEN_CALIBRATION);
}

static void hw_theme_btn_cb(lv_event_t *e)
{
    (void)e;
    sc_ui_router_push(SC_UI_SCREEN_THEME_SELECTOR);
}

static void factory_reset_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (s_wifi_modal) {
        lv_obj_delete(s_wifi_modal);
        s_wifi_modal = NULL;
    }
}

static void factory_reset_confirm_cb(lv_event_t *e)
{
    (void)e;
    sc_config_factory_reset();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void hw_reset_btn_cb(lv_event_t *e)
{
    (void)e;
    s_wifi_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_wifi_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_wifi_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_wifi_modal, LV_OPA_70, 0);
    lv_obj_set_flex_flow(s_wifi_modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_wifi_modal, 24, 0);
    lv_obj_set_style_pad_gap(s_wifi_modal, 16, 0);
    lv_obj_set_style_border_width(s_wifi_modal, 0, 0);

    lv_obj_t *card = lv_obj_create(s_wifi_modal);
    lv_obj_set_size(card, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(card, SC_COL_BG_PANEL, 0);
    lv_obj_set_style_border_color(card, SC_COL_ARMED, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_set_style_pad_gap(card, 20, 0);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "FACTORY RESET");
    lv_obj_set_style_text_font(title, SC_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, SC_COL_ARMED, 0);

    lv_obj_t *desc = lv_label_create(card);
    lv_label_set_text(desc, "Are you sure you want to clear NVS settings and trigger a factory reset? This will restart the device.");
    lv_obj_set_style_text_font(desc, SC_FONT_SMALL, 0);
    lv_obj_set_style_text_color(desc, SC_COL_TEXT, 0);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(desc, LV_PCT(100));
    
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 16, 0);

    lv_obj_t *cancel_btn = lv_button_create(row);
    lv_obj_set_style_bg_color(cancel_btn, SC_COL_BG, 0);
    lv_obj_set_style_border_color(cancel_btn, SC_COL_DIVIDER, 0);
    lv_obj_set_style_border_width(cancel_btn, 1, 0);
    lv_obj_add_event_cb(cancel_btn, factory_reset_cancel_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");

    lv_obj_t *reset_btn = lv_button_create(row);
    lv_obj_set_style_bg_color(reset_btn, SC_COL_ARMED, 0);
    lv_obj_add_event_cb(reset_btn, factory_reset_confirm_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "RESET");
    lv_obj_set_style_text_color(reset_lbl, lv_color_white(), 0);
}

static void draw_hardware_tab(void)
{
    lv_obj_t *title = lv_label_create(s_content_inner);
    lv_label_set_text(title, "Hardware & Calibration");
    lv_obj_set_style_text_font(title, SC_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(title, SC_COL_ACCENT, 0);

    lv_obj_t *bright_lbl = lv_label_create(s_content_inner);
    lv_label_set_text(bright_lbl, "Backlight Brightness");
    lv_obj_set_style_text_font(bright_lbl, SC_FONT_SMALL, 0);
    lv_obj_set_style_text_color(bright_lbl, SC_COL_TEXT_DIM, 0);

    lv_obj_t *bright_row = lv_obj_create(s_content_inner);
    lv_obj_set_size(bright_row, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bright_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(bright_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bright_row, 0, 0);
    lv_obj_set_style_pad_all(bright_row, 0, 0);
    lv_obj_set_style_pad_gap(bright_row, 15, 0);

    uint8_t current_brightness = get_nvs_brightness();

    lv_obj_t *slider = lv_slider_create(bright_row);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, current_brightness, LV_ANIM_OFF);
    lv_obj_set_width(slider, 200);
    lv_obj_add_event_cb(slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, brightness_slider_released_cb, LV_EVENT_RELEASED, NULL);

    s_brightness_val_lbl = lv_label_create(bright_row);
    lv_label_set_text_fmt(s_brightness_val_lbl, "%d%%", current_brightness);
    lv_obj_set_style_text_font(s_brightness_val_lbl, SC_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(s_brightness_val_lbl, SC_COL_TEXT, 0);

    lv_obj_t *cal_btn = lv_button_create(s_content_inner);
    lv_obj_set_size(cal_btn, LV_PCT(90), 40);
    lv_obj_set_style_bg_color(cal_btn, SC_COL_ACCENT, 0);
    lv_obj_add_event_cb(cal_btn, hw_calibrate_btn_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *cal_lbl = lv_label_create(cal_btn);
    lv_label_set_text(cal_lbl, "CALIBRATE TOUCH CONTROLLER");
    lv_obj_set_style_text_color(cal_lbl, SC_COL_BG, 0);
    lv_obj_center(cal_lbl);

    lv_obj_t *theme_btn = lv_button_create(s_content_inner);
    lv_obj_set_size(theme_btn, LV_PCT(90), 40);
    lv_obj_set_style_bg_color(theme_btn, SC_COL_ACCENT, 0);
    lv_obj_add_event_cb(theme_btn, hw_theme_btn_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *theme_lbl = lv_label_create(theme_btn);
    lv_label_set_text(theme_lbl, "OPEN THEME SELECTOR");
    lv_obj_set_style_text_color(theme_lbl, SC_COL_BG, 0);
    lv_obj_center(theme_lbl);

    lv_obj_t *reset_btn = lv_button_create(s_content_inner);
    lv_obj_set_size(reset_btn, LV_PCT(90), 40);
    lv_obj_set_style_bg_color(reset_btn, SC_COL_ARMED, 0);
    lv_obj_add_event_cb(reset_btn, hw_reset_btn_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "FACTORY RESET ALL SETTINGS");
    lv_obj_set_style_text_color(reset_lbl, lv_color_white(), 0);
    lv_obj_center(reset_lbl);
}

/* ── Content Switcher ── */
static void draw_active_tab_content(void)
{
    if (s_content_inner) {
        lv_obj_delete(s_content_inner);
        s_content_inner = NULL;
    }
    if (s_status_timer) {
        lv_timer_delete(s_status_timer);
        s_status_timer = NULL;
    }

    for (int i = 0; i < TAB_COUNT; i++) {
        if (s_nav_buttons[i]) {
            if (i == s_active_tab) {
                lv_obj_set_style_bg_color(s_nav_buttons[i], SC_COL_ACCENT, 0);
                lv_obj_set_style_text_color(s_nav_buttons[i], SC_COL_BG, 0);
            } else {
                lv_obj_set_style_bg_color(s_nav_buttons[i], SC_COL_BG_PANEL, 0);
                lv_obj_set_style_text_color(s_nav_buttons[i], SC_COL_TEXT, 0);
            }
        }
    }

    s_content_inner = lv_obj_create(s_content_area);
    lv_obj_set_size(s_content_inner, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_content_inner, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(s_content_inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content_inner, 0, 0);
    lv_obj_set_style_pad_all(s_content_inner, 10, 0);
    lv_obj_set_style_pad_gap(s_content_inner, 12, 0);
    lv_obj_set_scrollbar_mode(s_content_inner, LV_SCROLLBAR_MODE_AUTO);

    switch (s_active_tab) {
        case TAB_STATUS:
            draw_status_tab();
            break;
        case TAB_TARGET:
            draw_target_tab();
            break;
        case TAB_WIFI:
            draw_wifi_tab();
            break;
        case TAB_HARDWARE:
            draw_hardware_tab();
            break;
        default:
            break;
    }
}

/* ── Public API ── */
lv_obj_t *sc_ui_screen_settings_create(lv_obj_t *parent)
{
    lv_lock();

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, SC_COL_BG, 0);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_root, SC_UI_PAD_MEDIUM, 0);
    lv_obj_set_style_pad_gap(s_root, 10, 0);

    /* ── Header Bar ── */
    lv_obj_t *header = lv_obj_create(s_root);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_gap(header, 16, 0);

    lv_obj_t *back_btn = lv_button_create(header);
    lv_obj_set_size(back_btn, 80, 36);
    lv_obj_set_style_bg_color(back_btn, SC_COL_ARMED, 0);
    lv_obj_add_event_cb(back_btn, back_btn_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "BACK");
    lv_obj_center(back_lbl);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "SC TERMINAL CONTROL PANEL");
    lv_obj_set_style_text_font(title, SC_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(title, SC_COL_TEXT, 0);

    /* ── Main Layout ── */
    lv_obj_t *main_row = lv_obj_create(s_root);
    lv_obj_set_size(main_row, LV_PCT(100), LV_PCT(85));
    lv_obj_set_flex_flow(main_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(main_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_row, 0, 0);
    lv_obj_set_style_pad_all(main_row, 0, 0);
    lv_obj_set_style_pad_gap(main_row, 12, 0);

    s_sidebar = lv_obj_create(main_row);
    lv_obj_set_size(s_sidebar, LV_PCT(25), LV_PCT(100));
    lv_obj_set_flex_flow(s_sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(s_sidebar, SC_COL_BG_PANEL, 0);
    lv_obj_set_style_border_color(s_sidebar, SC_COL_DIVIDER, 0);
    lv_obj_set_style_border_width(s_sidebar, 1, 0);
    lv_obj_set_style_pad_all(s_sidebar, 8, 0);
    lv_obj_set_style_pad_gap(s_sidebar, 10, 0);

    const char *tab_names[] = {"STATUS", "TARGET", "WI-FI", "HARDWARE"};
    for (int i = 0; i < TAB_COUNT; i++) {
        s_nav_buttons[i] = lv_button_create(s_sidebar);
        lv_obj_set_size(s_nav_buttons[i], LV_PCT(100), 40);
        lv_obj_add_event_cb(s_nav_buttons[i], nav_btn_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_t *lbl = lv_label_create(s_nav_buttons[i]);
        lv_label_set_text(lbl, tab_names[i]);
        lv_obj_set_style_text_font(lbl, SC_FONT_SMALL, 0);
        lv_obj_center(lbl);
    }

    s_content_area = lv_obj_create(main_row);
    lv_obj_set_size(s_content_area, LV_PCT(72), LV_PCT(100));
    s_content_area = sc_ui_theme_draw_panel(s_content_area);

    s_content_inner = NULL;
    draw_active_tab_content();

    lv_unlock();
    return s_root;
}

void sc_ui_screen_settings_load(const sc_terminal_config_t *cfg)
{
    (void)cfg;
}

void sc_ui_screen_settings_destroy(void)
{
    lv_lock();
    if (s_status_timer) {
        lv_timer_delete(s_status_timer);
        s_status_timer = NULL;
    }
    if (s_wifi_modal) {
        lv_obj_delete(s_wifi_modal);
        s_wifi_modal = NULL;
    }
    if (s_root) {
        lv_obj_delete(s_root);
        s_root = NULL;
    }
    lv_unlock();
}

void sc_ui_screen_settings_on_event(const sc_gamelink_event_t *evt)
{
    (void)evt;
}
