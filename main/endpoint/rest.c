#include "audio_hal.h"
#include "audio_thread.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "http.h"
#include "shared.h"
#include "slvgl.h"
#include "timer.h"

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

    if (ok) {
        war.fn_ok("ok");
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
