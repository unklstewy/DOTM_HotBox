/*
 * sc_gamelink.c — Game.log event dispatcher
 *
 * Subscribes to incoming WebSocket frames from sc_network.
 * Parses each JSON frame, identifies the event type, and calls
 * all registered handlers for that event.
 *
 * Event frame format:
 * { "event": "<event_id>", "ship": "<ship_id>", "ts": <ms>, "data": {...} }
 */

#include "sc_gamelink.h"
#include "sc_network.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "sc_gamelink";

/* ── Handler registry ────────────────────────────────────────────────────── */
typedef struct {
    char                   event_id[SC_GAMELINK_EVENT_ID_LEN];
    sc_gamelink_handler_fn_t handler;
    void                  *user_ctx;
    bool                   active;
} sc_gamelink_handler_entry_t;

static sc_gamelink_handler_entry_t s_handlers[SC_GAMELINK_MAX_HANDLERS];

/* ── Incoming frame queue ────────────────────────────────────────────────── */
typedef struct {
    char  *json_str;   /* heap-allocated; freed by gamelink task */
    size_t len;
} gamelink_frame_t;

static QueueHandle_t s_frame_q;

/* ── Task ────────────────────────────────────────────────────────────────── */
static StaticTask_t s_task_buf;
static StackType_t  s_task_stack[SC_GAMELINK_TASK_STACK_SIZE];
static TaskHandle_t s_task_handle = NULL;

/* ── Forward declarations ────────────────────────────────────────────────── */
static void sc_gamelink_task(void *arg);
static void sc_gamelink_ws_rx(const char *data, size_t len, void *ctx);
static void sc_gamelink_dispatch(const char *json_str, size_t len);
static void sc_gamelink_parse_gear(cJSON *data_obj, sc_gamelink_event_t *evt);
static void sc_gamelink_parse_shield(cJSON *data_obj, sc_gamelink_event_t *evt);
static void sc_gamelink_parse_quantum(cJSON *data_obj, sc_gamelink_event_t *evt);

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

esp_err_t sc_gamelink_init(void)
{
    memset(s_handlers, 0, sizeof(s_handlers));

    s_frame_q = xQueueCreate(16, sizeof(gamelink_frame_t));
    if (!s_frame_q) return ESP_ERR_NO_MEM;

    /* Register WebSocket receive callback in sc_network */
    esp_err_t ret = sc_network_ws_rx_register(sc_gamelink_ws_rx, NULL);
    if (ret != ESP_OK) return ret;

    s_task_handle = xTaskCreateStatic(
        sc_gamelink_task,
        "sc_gamelink",
        SC_GAMELINK_TASK_STACK_SIZE,
        NULL,
        SC_GAMELINK_TASK_PRIORITY,
        s_task_stack,
        &s_task_buf
    );
    if (!s_task_handle) return ESP_FAIL;

    ESP_LOGI(TAG, "GameLink started");
    return ESP_OK;
}

void sc_gamelink_deinit(void)
{
    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }
    sc_network_ws_rx_register(NULL, NULL);
    vQueueDelete(s_frame_q);
}

/* ── Handler registration ────────────────────────────────────────────────── */

esp_err_t sc_gamelink_handler_register(const char             *event_id,
                                       sc_gamelink_handler_fn_t handler,
                                       void                   *user_ctx)
{
    if (!event_id || !handler) return ESP_ERR_INVALID_ARG;

    for (int i = 0; i < SC_GAMELINK_MAX_HANDLERS; i++) {
        if (!s_handlers[i].active) {
            strlcpy(s_handlers[i].event_id, event_id,
                    sizeof(s_handlers[i].event_id));
            s_handlers[i].handler  = handler;
            s_handlers[i].user_ctx = user_ctx;
            s_handlers[i].active   = true;
            ESP_LOGD(TAG, "Handler registered: %s [slot %d]", event_id, i);
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "Handler table full");
    return ESP_ERR_NO_MEM;
}

esp_err_t sc_gamelink_handler_deregister(const char             *event_id,
                                         sc_gamelink_handler_fn_t handler)
{
    for (int i = 0; i < SC_GAMELINK_MAX_HANDLERS; i++) {
        if (s_handlers[i].active &&
            strcmp(s_handlers[i].event_id, event_id) == 0 &&
            s_handlers[i].handler == handler) {
            memset(&s_handlers[i], 0, sizeof(s_handlers[i]));
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

/* ── WebSocket receive callback (called from network task) ───────────────── */

static void sc_gamelink_ws_rx(const char *data, size_t len, void *ctx)
{
    (void)ctx;
    char *copy = malloc(len + 1);
    if (!copy) {
        ESP_LOGW(TAG, "OOM — dropping frame");
        return;
    }
    memcpy(copy, data, len);
    copy[len] = '\0';

    gamelink_frame_t frame = { .json_str = copy, .len = len };
    if (xQueueSend(s_frame_q, &frame, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Frame queue full — dropping");
        free(copy);
    }
}

/* ── GameLink task ────────────────────────────────────────────────────────── */

static void sc_gamelink_task(void *arg)
{
    gamelink_frame_t frame;
    for (;;) {
        if (xQueueReceive(s_frame_q, &frame, pdMS_TO_TICKS(100)) == pdTRUE) {
            sc_gamelink_dispatch(frame.json_str, frame.len);
            free(frame.json_str);
        }
    }
}

/* ── JSON dispatch ───────────────────────────────────────────────────────── */

static void sc_gamelink_dispatch(const char *json_str, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json_str, len);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse error");
        return;
    }

    sc_gamelink_event_t evt = {0};

    cJSON *ev_item   = cJSON_GetObjectItem(root, "event");
    cJSON *ship_item = cJSON_GetObjectItem(root, "ship");
    cJSON *ts_item   = cJSON_GetObjectItem(root, "ts");
    cJSON *data_obj  = cJSON_GetObjectItem(root, "data");

    if (!cJSON_IsString(ev_item)) goto cleanup;

    strlcpy(evt.event_id, ev_item->valuestring, sizeof(evt.event_id));
    if (cJSON_IsString(ship_item)) {
        strlcpy(evt.ship_id, ship_item->valuestring, sizeof(evt.ship_id));
    }
    if (cJSON_IsNumber(ts_item)) {
        evt.timestamp_ms = (int64_t)ts_item->valuedouble;
    }

    /* Parse typed payload */
    if (strcmp(evt.event_id, SC_GAMELINK_EVT_GEAR_STATE) == 0) {
        sc_gamelink_parse_gear(data_obj, &evt);
    } else if (strcmp(evt.event_id, SC_GAMELINK_EVT_SHIELD_STATE) == 0) {
        sc_gamelink_parse_shield(data_obj, &evt);
    } else if (strcmp(evt.event_id, SC_GAMELINK_EVT_QUANTUM_STATE) == 0) {
        sc_gamelink_parse_quantum(data_obj, &evt);
    } else if (cJSON_IsObject(data_obj)) {
        /* Fall back: store raw JSON in evt.payload.raw */
        cJSON_PrintPreallocated(data_obj, evt.payload.raw,
                                SC_GAMELINK_MAX_PAYLOAD_LEN - 1, false);
    }

    /* Dispatch to all registered handlers for this event */
    for (int i = 0; i < SC_GAMELINK_MAX_HANDLERS; i++) {
        if (s_handlers[i].active &&
            strcmp(s_handlers[i].event_id, evt.event_id) == 0) {
            s_handlers[i].handler(&evt, s_handlers[i].user_ctx);
        }
    }

cleanup:
    cJSON_Delete(root);
}

/* ── Per-event parsers ───────────────────────────────────────────────────── */

static void sc_gamelink_parse_gear(cJSON *d, sc_gamelink_event_t *evt)
{
    if (!d) return;
    cJSON *state = cJSON_GetObjectItem(d, "state");
    if (cJSON_IsString(state)) {
        strlcpy(evt->payload.gear.state, state->valuestring,
                sizeof(evt->payload.gear.state));
    }
}

static void sc_gamelink_parse_shield(cJSON *d, sc_gamelink_event_t *evt)
{
    if (!d) return;
    cJSON *f = cJSON_GetObjectItem(d, "front");
    cJSON *b = cJSON_GetObjectItem(d, "back");
    cJSON *l = cJSON_GetObjectItem(d, "left");
    cJSON *r = cJSON_GetObjectItem(d, "right");
    if (cJSON_IsNumber(f)) evt->payload.shield.front_pct = (float)f->valuedouble;
    if (cJSON_IsNumber(b)) evt->payload.shield.back_pct  = (float)b->valuedouble;
    if (cJSON_IsNumber(l)) evt->payload.shield.left_pct  = (float)l->valuedouble;
    if (cJSON_IsNumber(r)) evt->payload.shield.right_pct = (float)r->valuedouble;
}

static void sc_gamelink_parse_quantum(cJSON *d, sc_gamelink_event_t *evt)
{
    if (!d) return;
    cJSON *state = cJSON_GetObjectItem(d, "state");
    cJSON *spool = cJSON_GetObjectItem(d, "spool_pct");
    if (cJSON_IsString(state)) {
        strlcpy(evt->payload.quantum.state, state->valuestring,
                sizeof(evt->payload.quantum.state));
    }
    if (cJSON_IsNumber(spool)) {
        evt->payload.quantum.spool_pct = (float)spool->valuedouble;
    }
}
