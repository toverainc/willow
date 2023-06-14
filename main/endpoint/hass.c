#include "audio_hal.h"
#include "audio_thread.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_transport_ws.h"
#include "esp_websocket_client.h"
#include "lvgl.h"
#include "sdkconfig.h"

#include "../http.h"
#include "shared.h"
#include "slvgl.h"
#include "timer.h"

#define HASS_SPEECH_MAX_LEN           64
#define HASS_URI_COMPONENTS           "/api/components"
#define HASS_URI_CONVERSATION_PROCESS "/api/conversation/process"
#define HASS_URI_WEBSOCKET            "/api/websocket"
#define STRLEN(s)                     strlen(s)
#define URL_CLEN                      (8 + 1 + 5 + 1) // https:// + : + $PORT + NULL terminator

#ifdef CONFIG_HOMEASSISTANT_TLS
#define HOMEASSISTANT_TLS true
#else
#define HOMEASSISTANT_TLS false
#endif

struct hass_intent_response {
    bool has_speech;
    bool ok;
    char speech[HASS_SPEECH_MAX_LEN];
};

static struct hass_intent_response hir;

static bool has_assist_pipeline = false;
static esp_websocket_client_handle_t hdl_wc = NULL;

static void cb_ws_event(const void *arg_evh, const esp_event_base_t *base_ev, const int32_t id_ev, const void *ev_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)ev_data;
    switch (id_ev) {
        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == WS_TRANSPORT_OPCODES_TEXT) {
                char *json = NULL;
                char *resp = strndup((char *)data->data_ptr, data->data_len);

                ESP_LOGD(TAG, "received text data on WebSocket: %s", resp);

                cJSON *cjson = cJSON_Parse(resp);
                if (!cJSON_IsObject(cjson)) {
                    goto cleanup;
                }

                cJSON *event = cJSON_GetObjectItemCaseSensitive(cjson, "event");
                if (!cJSON_IsObject(event)) {
                    goto cleanup;
                }

                cJSON *type = cJSON_GetObjectItemCaseSensitive(event, "type");
                if (!cJSON_IsString(type) || type->valuestring == NULL) {
                    goto cleanup;
                }

                if (strcmp(type->valuestring, "run-end") == 0) {
                    goto end;
                }

                if (strcmp(type->valuestring, "intent-end") != 0) {
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
                    strncpy(hir.speech, speech2->valuestring, HASS_SPEECH_MAX_LEN - 1);
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

                lvgl_port_lock(0);
                lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
                lv_obj_align(lbl_ln3, LV_ALIGN_TOP_LEFT, 0, 120);
                lv_obj_remove_event_cb(lbl_ln3, cb_btn_cancel);
                if (hir.has_speech) {
                    lv_label_set_text_static(lbl_ln3, "Response:");
                    lv_label_set_text(lbl_ln4, hir.speech);
                    hir.ok ? war.fn_ok(hir.speech) : war.fn_err(hir.speech);
                } else {
                    lv_label_set_text_static(lbl_ln3, "Command status:");
                    lv_label_set_text(lbl_ln4, hir.ok ? "#008000 Success!" : "#ff0000 Error!");
                    hir.ok ? war.fn_ok("success") : war.fn_err("error");
                }
                lvgl_port_unlock();

                reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, false);

cleanup:
                cJSON_Delete(cjson);
                free(resp);
            }
            break;
        default:
            ESP_LOGI(TAG, "WS event ID: %d", id_ev);
            break;
    }
}

static void hass_get_url(char **url, const char *path, const bool ws)
{
    int len_url = 0;
#if HOMEASSISTANT_TLS
    char *scheme = ws ? "wss" : "https";
#else
    char *scheme = ws ? "ws" : "http";
#endif

    len_url = URL_CLEN + STRLEN(CONFIG_HOMEASSISTANT_HOST) + strlen(path);
    if (path != NULL) {
        len_url += strlen(path);
    }
    *url = calloc(sizeof(char), len_url);
    snprintf(*url, len_url, "%s://%s:%d%s", scheme, CONFIG_HOMEASSISTANT_HOST, CONFIG_HOMEASSISTANT_PORT,
             path ? path : "");

    ESP_LOGI(TAG, "HASS URL: %s", *url);
}

static void init_hass_ws_client(void)
{
    char *auth = NULL;
    char *url = NULL;
    esp_err_t err;
    int len_auth, ret;

    hass_get_url(&url, HASS_URI_WEBSOCKET, true);

    const esp_websocket_client_config_t cfg_wc = {
        .buffer_size = 4096,
        .path = HASS_URI_WEBSOCKET,
        .uri = url,
    };

    hdl_wc = esp_websocket_client_init(&cfg_wc);
    free(url);

    esp_websocket_register_events(hdl_wc, WEBSOCKET_EVENT_ANY, (esp_event_handler_t)cb_ws_event, NULL);

    err = esp_websocket_client_start(hdl_wc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start WebSocket client: %s", esp_err_to_name(err));
        return;
    }

    len_auth = strlen(CONFIG_HOMEASSISTANT_TOKEN) + 34;
    auth = calloc(sizeof(char), len_auth);
    snprintf(auth, len_auth, "{\"type\":\"auth\",\"access_token\":\"%s\"}", CONFIG_HOMEASSISTANT_TOKEN);

    // we must not send the terminating null byte
    ret = esp_websocket_client_send_text(hdl_wc, auth, len_auth - 1, 2000 / portTICK_PERIOD_MS);
    free(auth);
    if (ret < 0) {
        ESP_LOGE(TAG, "failed to authenticate WebSocket client");
    }
}

static esp_err_t hass_set_http_auth(const esp_http_client_handle_t hdl_hc)
{
    char *hdr_auth = calloc(sizeof(char), 8 + strlen(CONFIG_HOMEASSISTANT_TOKEN));
    snprintf(hdr_auth, 8 + strlen(CONFIG_HOMEASSISTANT_TOKEN), "Bearer %s", CONFIG_HOMEASSISTANT_TOKEN);
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
    ret = hass_set_http_auth(hdl_hc);

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
    ret = hass_set_http_auth(hdl_hc);

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
    }

http_error:
    lvgl_port_lock(0);
    lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(lbl_ln3, LV_ALIGN_TOP_LEFT, 0, 120);
    lv_obj_remove_event_cb(lbl_ln3, cb_btn_cancel);
    if (http_status == 200) {
        lv_label_set_text_static(lbl_ln3, "Command status:");
        lv_label_set_text(lbl_ln4, ok ? "#008000 Success!" : "#ff0000 No Matching HA Intent");
    } else {
        lv_label_set_text_static(lbl_ln3, "Error contacting HASS:");
        lv_label_set_text_fmt(lbl_ln4, "#ff0000 HTTP %d", http_status);
    }

    lvgl_port_unlock();

    reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, false);
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

    cJSON *ws_input = cJSON_CreateObject();
    if (ws_input == NULL) {
        ESP_LOGE(TAG, "failed to create ws_input JSON object");
    }

    if (cJSON_IsString(text) && text->valuestring != NULL) {
        cJSON_AddItemToObject(ws_input, "text", text);
    } else {
        cJSON *text_ = cJSON_CreateString(data);
        cJSON_AddItemToObject(ws_input, "text", text_);
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
    memset(hir.speech, '\0', sizeof(hir.speech));

    cJSON *end_stage = cJSON_CreateString("intent");
    cJSON *id = cJSON_CreateNumber(tv_now.tv_sec);
    cJSON *start_stage = cJSON_CreateString("intent");
    cJSON *type = cJSON_CreateString("assist_pipeline/run");

    cJSON_AddItemToObject(ws_data, "end_stage", end_stage);
    cJSON_AddItemToObject(ws_data, "id", id);
    cJSON_AddItemToObject(ws_data, "input", ws_input);
    cJSON_AddItemToObject(ws_data, "start_stage", start_stage);
    cJSON_AddItemToObject(ws_data, "type", type);

    char *string = cJSON_Print(ws_data);

    ESP_LOGI(TAG, "sending command to Home Assistant via WebSocket: %s", string);

    ret = esp_websocket_client_send_text(hdl_wc, string, strlen(string), 2000 / portTICK_PERIOD_MS);
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
