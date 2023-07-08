#include "audio_hal.h"
#include "audio_thread.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "http.h"
#include "shared.h"
#include "slvgl.h"
#include "timer.h"

#define REST_SPEECH_MAX_LEN CONFIG_WILLOW_ENDPOINT_REST_SPEECH_MAX_LEN
static char rest_speech[REST_SPEECH_MAX_LEN];

void rest_send(const char *data)
{
    bool ok = false;
    char *body = NULL;
    char *url = CONFIG_WILLOW_ENDPOINT_REST_URL;
    esp_err_t ret;
    int http_status;

    esp_http_client_handle_t hdl_hc = init_http_client();

#if defined(CONFIG_WILLOW_ENDPOINT_REST_AUTH_AUTH_HEADER)
    ret = esp_http_client_set_header(hdl_hc, "Authorization", CONFIG_WILLOW_ENDPOINT_REST_AUTH_HEADER);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set authorization header: %s", esp_err_to_name(ret));
    }
#elif defined(CONFIG_WILLOW_ENDPOINT_REST_AUTH_BASIC)
    ret = http_set_basic_auth(hdl_hc, CONFIG_WILLOW_ENDPOINT_REST_AUTH_USERNAME,
                              CONFIG_WILLOW_ENDPOINT_REST_AUTH_PASSWORD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to enable HTTP Basic Authentication: %s", esp_err_to_name(ret));
    }
#endif

    ret = http_post(hdl_hc, url, "application/json", data, &body, &http_status);
    if (ret == ESP_OK) {
        if (http_status >= 200 && http_status <= 299) {
            ok = true;
        }
    } else {
        ESP_LOGE(TAG, "failed to read HTTP POST response");
    }

    if (ok) { // if doing audio responses and have "message" in the REST response JSON then copy to war.fn_ok
#if defined(CONFIG_WILLOW_AUDIO_RESPONSE_FS) || defined(CONFIG_WILLOW_AUDIO_RESPONSE_WIS_TTS)
        cJSON *cjson = cJSON_Parse(body);
        if (cJSON_IsObject(cjson)) {
          cJSON *message = cJSON_GetObjectItemCaseSensitive(cjson, "message");
          if (cJSON_IsString(message) && message->valuestring != NULL && strlen(message->valuestring) > 0) {
              strncpy(rest_speech, message->valuestring, REST_SPEECH_MAX_LEN - 1);
              war.fn_ok(rest_speech);
          } else {
              war.fn_ok("ok");
          }
        }
        cJSON_Delete(cjson);
#else
        war.fn_ok("ok");
#endif
    } else {
        war.fn_err("error");
    }

    lvgl_port_lock(0);
    lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(lbl_ln3, LV_ALIGN_TOP_LEFT, 0, 120);
    lv_label_set_text_static(lbl_ln3, "Command status:");
    lv_obj_remove_event_cb(lbl_ln3, cb_btn_cancel);
    if (strlen(body) > 1) {
        ESP_LOGI(TAG, "REST response: %s", body);
        lv_label_set_text(lbl_ln4, body);
    } else {
        lv_label_set_text(lbl_ln4, ok ? "#008000 Success!" : "#ff0000 Error");
    }
    lvgl_port_unlock();

    reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, false);

    free(body);
}
