#include "audio_hal.h"
#include "audio_thread.h"
#include "cJSON.h"
#include "driver/timer.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "sdkconfig.h"

#include "shared.h"
#include "slvgl.h"

#define HASS_URI_CONVERSATION_PROCESS "/api/conversation/process"
#define STRLEN(s)                     strlen(s)
#define URL_CLEN                      (8 + 1 + 5 + 1) // https:// + : + $PORT + NULL terminator

#ifdef CONFIG_HOMEASSISTANT_TLS
#define HOMEASSISTANT_TLS true
#else
#define HOMEASSISTANT_TLS false
#endif

static esp_http_client_handle_t init_hass_http_client(void)
{
    char *hdr_auth = NULL;
    esp_http_client_config_t cfg_hc = {
        // either host and path or url should be set
        .url = "http://dummy",
    };

    esp_http_client_handle_t hdl_hc = esp_http_client_init(&cfg_hc);

    hdr_auth = malloc(8 + strlen(CONFIG_HOMEASSISTANT_TOKEN));
    snprintf(hdr_auth, 8 + strlen(CONFIG_HOMEASSISTANT_TOKEN), "Bearer %s", CONFIG_HOMEASSISTANT_TOKEN);
    esp_http_client_set_header(hdl_hc, "Authorization", hdr_auth);
    free(hdr_auth);

    return hdl_hc;
}

void hass_post(char *data)
{
    bool ok;
    char *body = NULL;
    char *json = NULL;
    char *url = NULL;
    esp_err_t ret;
    int len_url, n;

    esp_http_client_handle_t hdl_hc = init_hass_http_client();

    len_url = URL_CLEN + STRLEN(CONFIG_HOMEASSISTANT_HOST) + STRLEN(HASS_URI_CONVERSATION_PROCESS);
    url = malloc(len_url);
    snprintf(url, len_url, "%s://%s:%d%s", HOMEASSISTANT_TLS ? "https" : "http", CONFIG_HOMEASSISTANT_HOST,
             CONFIG_HOMEASSISTANT_PORT, HASS_URI_CONVERSATION_PROCESS);

    ESP_LOGI(TAG, "sending '%s' to Home Assistant API on '%s'", data, url);
    esp_http_client_set_url(hdl_hc, url);
    esp_http_client_set_method(hdl_hc, HTTP_METHOD_POST);
    esp_http_client_set_header(hdl_hc, "Content-Type", "application/json");
    ret = esp_http_client_open(hdl_hc, strlen(data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to open HTTP connection: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    n = esp_http_client_write(hdl_hc, data, strlen(data));
    if (n < 0) {
        ESP_LOGE(TAG, "failed to POST HTTP data");
        goto cleanup;
    }
    n = esp_http_client_fetch_headers(hdl_hc);
    if (n < 0) {
        ESP_LOGE(TAG, "failed to get HTTP headers");
        goto cleanup;
    }
    body = malloc(n + 1);
    n = esp_http_client_read_response(hdl_hc, body, n);
    if (n >= 0) {
        int http_status = esp_http_client_get_status_code(hdl_hc);
        ESP_LOGI(TAG, "HTTP POST status='%d' content_length='%d'", http_status,
                 esp_http_client_get_content_length(hdl_hc));
        if (http_status != 200) {
            ok = false;
            audio_thread_create(NULL, "play_tone_err", play_tone_err, NULL, 4 * 1024, 10, true, 1);
        }
        cJSON *cjson = cJSON_Parse(body);
        cJSON *response = cJSON_GetObjectItemCaseSensitive(cjson, "response");
        if (cJSON_IsObject(response)) {
            cJSON *response_type = cJSON_GetObjectItemCaseSensitive(response, "response_type");
            if (cJSON_IsString(response_type) && response_type->valuestring != NULL) {
                ESP_LOGI(TAG, "home assistant response_type: %s", response_type->valuestring);
                if (!strcmp(response_type->valuestring, "error")) {
                    ok = false;
                    audio_thread_create(NULL, "play_tone_err", play_tone_err, NULL, 4 * 1024, 10, true, 1);
                } else {
                    ok = true;
                    audio_thread_create(NULL, "play_tone_ok", play_tone_ok, NULL, 4 * 1024, 10, true, 1);
                }
            }
        }
        json = cJSON_Print(cjson);
        cJSON_Delete(cjson);
        if (json != NULL) {
            ESP_LOGI(TAG, "HTTP POST response body:\n%s", json);
        }

        lvgl_port_lock(0);
        lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(lbl_ln3, LV_ALIGN_TOP_LEFT, 0, 120);
        lv_label_set_text_static(lbl_ln3, "Command status:");
        lv_obj_remove_event_cb(lbl_ln3, cb_btn_cancel);
        lv_label_set_text(lbl_ln4, ok ? "#008000 Success!" : "#ff0000 Something went wrong");
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "failed to read HTTP POST response");
    }
    timer_start(TIMER_GROUP_0, TIMER_0);
    free(body);
cleanup:
    esp_http_client_cleanup(hdl_hc);

    free(url);
}
