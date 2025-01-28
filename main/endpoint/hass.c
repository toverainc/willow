#include "audio_hal.h"
#include "audio_thread.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_transport_ws.h"
#include "esp_websocket_client.h"
#include "lvgl.h"
#include "sdkconfig.h"

#include "../http.h"
#include "audio.h"
#include "config.h"
#include "shared.h"
#include "slvgl.h"
#include "timer.h"

#define DEFAULT_HOST  "homeassistant.local"
#define DEFAULT_PORT  8123
#define DEFAULT_TLS   false
#define DEFAULT_TOKEN "your_ha_token"

#define HASS_URI_COMPONENTS           "/api/components"
#define HASS_URI_CONVERSATION_PROCESS "/api/conversation/process"
#define HASS_URI_WEBSOCKET            "/api/websocket"
#define STR_SCHEME_LEN                6
#define URL_CLEN                      (8 + 1 + 5 + 1) // https:// + : + $PORT + NULL terminator

struct hass_intent_response {
    bool has_speech;
    bool ok;
    char *speech;
};

static struct hass_intent_response hir;

static bool has_assist_pipeline = false;
static const char *TAG = "WILLOW/HASS";
static esp_websocket_client_handle_t hdl_wc = NULL;

static void init_hass_ws_client(void);

static void cb_ws_event(const void *arg_evh, const esp_event_base_t *base_ev, const int id_ev, const void *ev_data)
{
    switch (id_ev) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            break;
        case WEBSOCKET_EVENT_DATA:
            if (ev_data == NULL) {
                ESP_LOGW(TAG, "WEBSOCKET_EVENT_DATA: ev_data == NULL");
                break;
            }
            esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)ev_data;

            if (data->op_code == WS_TRANSPORT_OPCODES_TEXT) {
                char *json = NULL;

                if (data->data_ptr == NULL) {
                    ESP_LOGW(TAG, "WEBSOCKET_EVENT_DATA: data->data_ptr == NULL");
                    break;
                }
                if (data->data_len <= 0) {
                    ESP_LOGW(TAG, "WEBSOCKET_EVENT_DATA: data->data_len <= 0");
                    break;
                }
                char *resp = strndup((char *)data->data_ptr, data->data_len);

                ESP_LOGD(TAG, "received text data on WebSocket: %s", resp);

                cJSON *cjson = cJSON_Parse(resp);
                if (!cJSON_IsObject(cjson)) {
                    goto cleanup;
                }

                cJSON *type = cJSON_GetObjectItemCaseSensitive(cjson, "type");
                if (!cJSON_IsString(type) || type->valuestring == NULL) {
                    goto cleanup;
                }

                if (strcmp(type->valuestring, "auth_required") == 0) {
                    char *hass_token = config_get_char("hass_token", DEFAULT_TOKEN);
                    int len_auth = strlen(hass_token) + 34;
                    char *auth = calloc(sizeof(char), len_auth);
                    snprintf(auth, len_auth, "{\"type\":\"auth\",\"access_token\":\"%s\"}", hass_token);
                    free(hass_token);

                    // we must not send the terminating null byte
                    int ret = esp_websocket_client_send_text(hdl_wc, auth, len_auth - 1, 2000 / portTICK_PERIOD_MS);
                    free(auth);
                    if (ret < 0) {
                        ESP_LOGE(TAG, "failed to authenticate WebSocket client");
                    }
                    goto cleanup;
                }

                cJSON *event = cJSON_GetObjectItemCaseSensitive(cjson, "event");
                if (!cJSON_IsObject(event)) {
                    goto cleanup;
                }

                cJSON *event_type = cJSON_GetObjectItemCaseSensitive(event, "type");
                if (!cJSON_IsString(event_type) || event_type->valuestring == NULL) {
                    goto cleanup;
                }

                if (strcmp(event_type->valuestring, "run-end") == 0) {
                    goto end;
                }

                if (strcmp(event_type->valuestring, "intent-end") != 0) {
                    goto cleanup;
                }

                cJSON *event_data = cJSON_GetObjectItemCaseSensitive(event, "data");
                if (!cJSON_IsObject(event_data)) {
                    goto cleanup;
                }

                cJSON *intent_output = cJSON_GetObjectItemCaseSensitive(event_data, "intent_output");
                if (!cJSON_IsObject(intent_output)) {
                    goto cleanup;
                }

                cJSON *response = cJSON_GetObjectItemCaseSensitive(intent_output, "response");
                if (!cJSON_IsObject(response)) {
                    goto cleanup;
                }

                cJSON *speech = cJSON_GetObjectItemCaseSensitive(response, "speech");
                if (!cJSON_IsObject(speech)) {
                    goto no_speech;
                }

                cJSON *plain = cJSON_GetObjectItemCaseSensitive(speech, "plain");
                if (!cJSON_IsObject(plain)) {
                    goto no_speech;
                }

                cJSON *speech2 = cJSON_GetObjectItemCaseSensitive(plain, "speech");
                if (cJSON_IsString(speech2) && speech2->valuestring != NULL && strlen(speech2->valuestring) > 0) {
                    hir.has_speech = true;
                    hir.speech = calloc(sizeof(char), strlen(speech2->valuestring) + 1);
                    snprintf(hir.speech, strlen(speech2->valuestring) + 1, "%s", speech2->valuestring);
                }

no_speech:;
                cJSON *response_type = cJSON_GetObjectItemCaseSensitive(response, "response_type");
                if (cJSON_IsString(response_type) && response_type->valuestring != NULL) {
                    ESP_LOGI(TAG, "home assistant response_type: %s", response_type->valuestring);
                    if (strcmp(response_type->valuestring, "error") == 0) {
                        hir.ok = false;
                    } else {
                        hir.ok = true;
                    }
                    goto cleanup;
                }

end:
                json = cJSON_Print(cjson);
                ESP_LOGI(TAG, "received run-end event on WebSocket: %s", json);
                cJSON_free(json);

                if (hir.has_speech) {
                    hir.ok ? war.fn_ok(hir.speech) : war.fn_err(hir.speech);
                } else {
                    hir.ok ? war.fn_ok("Success") : war.fn_err("Error");
                }

                if (lvgl_port_lock(lvgl_lock_timeout)) {
                    lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(lbl_ln5, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_set_style_text_align(lbl_ln4, LV_TEXT_ALIGN_LEFT, 0);
                    lv_obj_set_style_text_align(lbl_ln5, LV_TEXT_ALIGN_LEFT, 0);
                    lv_obj_remove_event_cb(lbl_ln4, cb_btn_cancel);
                    if (hir.has_speech) {
                        lv_label_set_text_static(lbl_ln4, "Response:");
                        lv_label_set_text(lbl_ln5, hir.speech);
                    } else {
                        lv_label_set_text_static(lbl_ln4, "Command status:");
                        lv_label_set_text(lbl_ln5, hir.ok ? "Success!" : "Error");
                    }
                    lvgl_port_unlock();
                }

                if (hir.has_speech) {
                    free(hir.speech);
                }

                reset_timer(hdl_display_timer, config_get_int("display_timeout", DEFAULT_DISPLAY_TIMEOUT), false);

cleanup:
                cJSON_Delete(cjson);
                free(resp);
            }
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket disconnected");
            break;
        case WEBSOCKET_EVENT_CLOSED:
            ESP_LOGI(TAG, "WebSocket closed");
            init_hass_ws_client();
            break;
        default:
            ESP_LOGI(TAG, "WS event ID: %d", id_ev);
            break;
    }
}

static void hass_get_url(char **url, const char *path, const bool ws)
{
    char *hass_host;
    char scheme[STR_SCHEME_LEN];
    int len_url = 0;

    if (config_get_bool("hass_tls", DEFAULT_TLS)) {
        strncpy(scheme, ws ? "wss" : "https", STR_SCHEME_LEN);
    } else {
        strncpy(scheme, ws ? "ws" : "http", STR_SCHEME_LEN);
    }

    hass_host = config_get_char("hass_host", DEFAULT_HOST);
    len_url = URL_CLEN + strlen(hass_host) + strlen(path);
    if (path != NULL) {
        len_url += strlen(path);
    }
    *url = calloc(sizeof(char), len_url);
    snprintf(*url, len_url, "%s://%s:%d%s", scheme, hass_host, config_get_int("hass_port", DEFAULT_PORT),
             path ? path : "");
    free(hass_host);

    ESP_LOGI(TAG, "HASS URL: %s", *url);
}

static void init_hass_ws_client(void)
{
    char *url = NULL;
    hass_get_url(&url, HASS_URI_WEBSOCKET, true);

    const esp_websocket_client_config_t cfg_wc = {
        .buffer_size = 16384,
        .path = HASS_URI_WEBSOCKET,
        .task_stack = 6 * 1024, // default 4 * 1024
        .uri = url,
        .user_agent = WILLOW_USER_AGENT,
    };

    hdl_wc = esp_websocket_client_init(&cfg_wc);
    free(url);

    esp_websocket_register_events(hdl_wc, WEBSOCKET_EVENT_ANY, (esp_event_handler_t)cb_ws_event, NULL);

    esp_err_t err = esp_websocket_client_start(hdl_wc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start WebSocket client: %s", esp_err_to_name(err));
    }
}

static esp_err_t hass_set_http_auth(const esp_http_client_handle_t hdl_hc)
{
    char *hass_token = config_get_char("hass_token", DEFAULT_TOKEN);
    char *hdr_auth = calloc(sizeof(char), 8 + strlen(hass_token));
    snprintf(hdr_auth, 8 + strlen(hass_token), "Bearer %s", hass_token);
    free(hass_token);
    esp_err_t ret = esp_http_client_set_header(hdl_hc, "Authorization", hdr_auth);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set authorization header: %s", esp_err_to_name(ret));
    }
    free(hdr_auth);
    return ret;
}

static void hass_check_assist_pipeline(void)
{
    char *body = NULL;
    char *url = NULL;
    esp_err_t ret;
    int http_status;

    esp_http_client_handle_t hdl_hc = init_http_client();
    hass_set_http_auth(hdl_hc);

    hass_get_url(&url, HASS_URI_COMPONENTS, false);

    ret = http_get(hdl_hc, url, &body, &http_status);

    if (ret != ESP_OK || http_status != 200) {
        goto http_error;
    }

    cJSON *cjson = cJSON_Parse(body);
    if (cJSON_IsArray(cjson)) {
        cJSON *component = NULL;
        cJSON_ArrayForEach(component, cjson)
        {
            if (cJSON_IsString(component) && component->valuestring != NULL) {
                if (strcmp(component->valuestring, "assist_pipeline") == 0) {
                    ESP_LOGI(TAG, "Home Assistant has Assist Pipeline support");
                    has_assist_pipeline = true;
                    break;
                }
            }
        }
    }
    cJSON_Delete(cjson);

http_error:
    free(body);
    free(url);
}

static void hass_post(const char *data)
{
    bool ok = false;
    char *body = NULL;
    char *json = NULL;
    char *url = NULL;
    esp_err_t ret;
    int http_status;

    esp_http_client_handle_t hdl_hc = init_http_client();
    hass_set_http_auth(hdl_hc);

    hass_get_url(&url, HASS_URI_CONVERSATION_PROCESS, false);

    ESP_LOGI(TAG, "sending '%s' to Home Assistant API on '%s'", data, url);

    ret = http_post(hdl_hc, url, "application/json", data, &body, &http_status);
    if (ret != ESP_OK || http_status != 200) {
        ESP_LOGE(TAG, "hass_post: failed to contact Home Assistant: HTTP %d", http_status);
        war.fn_err("error");
        goto http_error;
    }

    cJSON *cjson = cJSON_Parse(body);
    cJSON *response = cJSON_GetObjectItemCaseSensitive(cjson, "response");
    if (cJSON_IsObject(response)) {
        cJSON *response_type = cJSON_GetObjectItemCaseSensitive(response, "response_type");
        if (cJSON_IsString(response_type) && response_type->valuestring != NULL) {
            ESP_LOGI(TAG, "home assistant response_type: %s", response_type->valuestring);
            if (!strcmp(response_type->valuestring, "error")) {
                war.fn_err("error");
            } else {
                ok = true;
                war.fn_ok("ok");
            }
        }
    }
    json = cJSON_Print(cjson);
    cJSON_Delete(cjson);
    if (json != NULL) {
        ESP_LOGI(TAG, "HTTP POST response body:\n%s", json);
        cJSON_free(json);
    }

http_error:
    if (lvgl_port_lock(lvgl_lock_timeout)) {
        lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_ln5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_event_cb(lbl_ln4, cb_btn_cancel);
        if (http_status == 200) {
            lv_label_set_text_static(lbl_ln4, "Command status:");
            lv_label_set_text(lbl_ln5, ok ? "Success!" : "No Matching HA Intent");
        } else {
            lv_label_set_text_static(lbl_ln4, "Error contacting HASS:");
            lv_label_set_text_fmt(lbl_ln5, "HTTP %d", http_status);
        }

        lvgl_port_unlock();
    }

    reset_timer(hdl_display_timer, config_get_int("display_timeout", DEFAULT_DISPLAY_TIMEOUT), false);
    free(body);

    free(url);
}

static void hass_send_ws(const char *data)
{
    int ret;

    if (!esp_websocket_client_is_connected(hdl_wc)) {
        esp_websocket_client_destroy(hdl_wc);
        init_hass_ws_client();
    }

    cJSON *cjson = cJSON_Parse(data);
    cJSON *text = cJSON_GetObjectItemCaseSensitive(cjson, "text");
    cJSON *text_ = NULL;

    cJSON *ws_input = cJSON_CreateObject();
    if (ws_input == NULL) {
        ESP_LOGE(TAG, "failed to create ws_input JSON object");
    }

    if (cJSON_IsString(text) && text->valuestring != NULL) {
        cJSON_AddItemToObjectCS(ws_input, "text", text);
    } else {
        text_ = cJSON_CreateStringReference(data);
        cJSON_AddItemToObjectCS(ws_input, "text", text_);
    }

    cJSON *ws_data = cJSON_CreateObject();
    if (ws_data == NULL) {
        ESP_LOGE(TAG, "failed to create ws_data JSON object");
    }

    // avoid {"id":123,"type":"result","success":false,"error":{"code":"id_reuse","message":"Identifier values have to
    // increase."}}
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);

    hir.has_speech = false;
    hir.ok = false;

    cJSON *end_stage = cJSON_CreateStringReference("intent");
    cJSON *id = cJSON_CreateNumber(tv_now.tv_sec);
    cJSON *start_stage = cJSON_CreateStringReference("intent");
    cJSON *type = cJSON_CreateStringReference("assist_pipeline/run");

    cJSON_AddItemToObjectCS(ws_data, "end_stage", end_stage);
    cJSON_AddItemToObjectCS(ws_data, "id", id);
    cJSON_AddItemToObjectCS(ws_data, "input", ws_input);
    cJSON_AddItemToObjectCS(ws_data, "start_stage", start_stage);
    cJSON_AddItemToObjectCS(ws_data, "type", type);

    char *string = cJSON_Print(ws_data);

    cJSON_free(end_stage);
    cJSON_free(id);
    cJSON_free(start_stage);
    cJSON_free(type);
    cJSON_free(ws_data);
    cJSON_free(ws_input);

    if (text_ != NULL) {
        cJSON_free(text_);
    }

    ESP_LOGI(TAG, "sending command to Home Assistant via WebSocket: %s", string);

    ret = esp_websocket_client_send_text(hdl_wc, string, strlen(string), 2000 / portTICK_PERIOD_MS);
    cJSON_free(string);
    if (ret < 0) {
        ESP_LOGE(TAG, "failed to send command via WebSocket client");
    }
    cJSON_Delete(cjson);
}

void hass_send(const char *data)
{
    if (has_assist_pipeline) {
        hass_send_ws(data);
    } else {
        hass_post(data);
    }
}

void init_hass(void)
{
    hass_check_assist_pipeline();
    if (has_assist_pipeline) {
        init_hass_ws_client();
    }
}

void hass_deinit_task(void *data)
{
    ESP_LOGI(TAG, "stopping WebSocket client");
    esp_err_t ret = esp_websocket_client_destroy(hdl_wc);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to stop WebSocket client: %s", esp_err_to_name(ret));
    }
    vTaskDelete(NULL);
}

void deinit_hass(void)
{
    if (hdl_wc == NULL) {
        return;
    }
    // needs to be done in a task to avoid this error:
    // WEBSOCKET_CLIENT: Client cannot be stopped from websocket task
    xTaskCreate(&hass_deinit_task, "hass_deinit_task", 4096, NULL, 5, NULL);
}
