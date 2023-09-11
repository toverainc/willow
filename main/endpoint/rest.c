#include "audio_hal.h"
#include "audio_thread.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "audio.h"
#include "config.h"
#include "http.h"
#include "shared.h"
#include "slvgl.h"
#include "timer.h"

#define DEFAULT_AUTH_HEADER ""
#define DEFAULT_AUTH_PASS   ""
#define DEFAULT_AUTH_TYPE   "None"
#define DEFAULT_AUTH_USER   ""
#define DEFAULT_URL         "http://your_rest_url"

static const char *TAG = "WILLOW/REST";

void rest_send(const char *data)
{
    bool ok = false;
    char *auth_type = NULL, *body = NULL, *pass = NULL, *url = NULL, *user = NULL;
    esp_err_t ret;
    int http_status;

    esp_http_client_handle_t hdl_hc = init_http_client();

    auth_type = config_get_char("rest_auth_type", DEFAULT_AUTH_TYPE);
    if (strcmp(auth_type, "Header") == 0) {
        char *auth_header = config_get_char("rest_auth_header", DEFAULT_AUTH_HEADER);
        ret = esp_http_client_set_header(hdl_hc, "Authorization", auth_header);
        free(auth_header);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "failed to set authorization header: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(auth_type, "Basic") == 0) {
        pass = config_get_char("rest_auth_pass", DEFAULT_AUTH_PASS);
        user = config_get_char("rest_auth_user", DEFAULT_AUTH_USER);
        ret = http_set_basic_auth(hdl_hc, user, pass);
        free(pass);
        free(user);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "failed to enable HTTP Basic Authentication: %s", esp_err_to_name(ret));
        }
    }
    free(auth_type);

    url = config_get_char("rest_url", DEFAULT_URL);
    ret = http_post(hdl_hc, url, "application/json", data, &body, &http_status);
    free(url);
    if (ret == ESP_OK) {
        if (http_status >= 200 && http_status <= 299) {
            ok = true;
        }
    } else {
        ESP_LOGE(TAG, "failed to read HTTP POST response");
    }

    if (ok) {
        if (body != NULL && strlen(body) > 1) {
            ESP_LOGI(TAG, "REST response: %s", body);
            war.fn_ok(body);
        } else {
            ESP_LOGI(TAG, "REST successful");
            war.fn_ok("Success");
        }
    } else {
        ESP_LOGI(TAG, "REST failed");
        war.fn_err("Something went wrong");
    }

    if (lvgl_port_lock(lvgl_lock_timeout)) {
        lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_ln5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_event_cb(lbl_ln4, cb_btn_cancel);
        if (body != NULL && strlen(body) > 1) {
            lv_label_set_text_static(lbl_ln4, "Response:");
            lv_label_set_text(lbl_ln5, body);
        } else {
            lv_label_set_text_static(lbl_ln4, "Command status:");
            lv_label_set_text(lbl_ln5, ok ? "#008000 Success!" : "#ff0000 Error");
        }
        lvgl_port_unlock();
    }

    reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, false);

    free(body);
}
