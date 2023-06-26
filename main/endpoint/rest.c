#include "audio_hal.h"
#include "audio_thread.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "audio.h"
#include "config.h"
#include "http.h"
#include "slvgl.h"
#include "timer.h"

static const char *TAG = "WILLOW/REST";

void rest_send(const char *data)
{
    bool ok = false;
    char *body = NULL;
    esp_err_t ret;
    int http_status;

    esp_http_client_handle_t hdl_hc = init_http_client();

    if (strcmp(config_get_char("rest_auth_type"), "Header") == 0) {
        ret = esp_http_client_set_header(hdl_hc, "Authorization", config_get_char("rest_auth_header"));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "failed to set authorization header: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(config_get_char("rest_auth_type"), "Basic") == 0) {
        ret = http_set_basic_auth(hdl_hc, config_get_char("rest_auth_user"), config_get_char("rest_auth_pass"));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "failed to enable HTTP Basic Authentication: %s", esp_err_to_name(ret));
        }
    }

    ret = http_post(hdl_hc, config_get_char("rest_url"), "application/json", data, &body, &http_status);
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
    lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lbl_ln5, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_static(lbl_ln4, "Command status:");
    lv_obj_remove_event_cb(lbl_ln4, cb_btn_cancel);
    if (strlen(body) > 1) {
        ESP_LOGI(TAG, "REST response: %s", body);
        lv_label_set_text(lbl_ln5, body);
    } else {
        lv_label_set_text(lbl_ln5, ok ? "#008000 Success!" : "#ff0000 Error");
    }
    lvgl_port_unlock();

    reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, false);

    free(body);
}
