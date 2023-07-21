#include "cJSON.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_transport_ws.h"
#include "esp_websocket_client.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include "config.h"
#include "display.h"
#include "network.h"
#include "ota.h"
#include "shared.h"
#include "slvgl.h"
#include "system.h"
#include "timer.h"
#include "was.h"

static const char *TAG = "WILLOW/WAS";
static esp_websocket_client_handle_t hdl_wc = NULL;

static void send_hello(void);

static void cb_ws_event(const void *arg_evh, const esp_event_base_t *base_ev, const int32_t id_ev, const void *ev_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)ev_data;
    // components/esp_websocket_client/include/esp_websocket_client.h - enum esp_websocket_event_id_t
    switch (id_ev) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            send_hello();
            break;
        case WEBSOCKET_EVENT_DATA:
            ESP_LOGV(TAG, "WebSocket data received");
            if (data->op_code == WS_TRANSPORT_OPCODES_TEXT) {
                char *resp = strndup((char *)data->data_ptr, data->data_len);
                ESP_LOGI(TAG, "received text data on WebSocket: %s", resp);
                cJSON *cjson = cJSON_Parse(resp);
                cJSON *json_config = cJSON_GetObjectItemCaseSensitive(cjson, "config");
                if (cJSON_IsObject(json_config)) {
                    char *config = cJSON_Print(json_config);
                    ESP_LOGI(TAG, "found config in WebSocket message: %s", config);
                    config_write(config);
                    cJSON_free(config);
                    goto cleanup;
                }

                cJSON *json_nvs = cJSON_GetObjectItemCaseSensitive(cjson, "nvs");
                if (cJSON_IsObject(json_nvs)) {
                    char *nvs = cJSON_Print(json_nvs);
                    esp_err_t ret = ESP_OK;
                    nvs_handle_t hdl_nvs;

                    ESP_LOGI(TAG, "found nvs in WebSocket message: %s", nvs);
                    cJSON_free(nvs);

                    cJSON *json_was = cJSON_GetObjectItemCaseSensitive(json_nvs, "WAS");
                    if (cJSON_IsObject(json_was)) {
                        ESP_LOGD(TAG, "found WAS in nvs message");
                        ret = nvs_open("WAS", NVS_READWRITE, &hdl_nvs);
                        if (ret != ESP_OK) {
                            ESP_LOGE(TAG, "failed to open NVS namespace WAS");
                            goto cleanup;
                        }
                        cJSON *json_was_url = cJSON_GetObjectItemCaseSensitive(json_was, "URL");
                        if (cJSON_IsString(json_was_url) && json_was_url->valuestring != NULL) {
                            ESP_LOGD(TAG, "found WAS URL in nvs message");
                            ret = nvs_set_str(hdl_nvs, "URL", json_was_url->valuestring);
                            if (ret != ESP_OK) {
                                ESP_LOGE(TAG, "failed to set URL in NVS namespace WAS");
                                goto cleanup;
                            }
                        }
                    }

                    cJSON *json_wifi = cJSON_GetObjectItemCaseSensitive(json_nvs, "WIFI");
                    if (cJSON_IsObject(json_wifi)) {
                        ESP_LOGD(TAG, "found WIFI in nvs message");
                        ret = nvs_open("WIFI", NVS_READWRITE, &hdl_nvs);
                        if (ret != ESP_OK) {
                            ESP_LOGE(TAG, "failed to open NVS namespace WIFI");
                            goto cleanup;
                        }
                        cJSON *json_wifi_psk = cJSON_GetObjectItemCaseSensitive(json_wifi, "PSK");
                        if (cJSON_IsString(json_wifi_psk) && json_wifi_psk->valuestring != NULL) {
                            ESP_LOGD(TAG, "found WIFI PSK in nvs message");
                            ret = nvs_set_str(hdl_nvs, "PSK", json_wifi_psk->valuestring);
                            if (ret != ESP_OK) {
                                ESP_LOGE(TAG, "failed to set PSK in NVS namespace WIFI");
                                goto cleanup;
                            }
                        }
                        cJSON *json_wifi_ssid = cJSON_GetObjectItemCaseSensitive(json_wifi, "SSID");
                        if (cJSON_IsString(json_wifi_ssid) && json_wifi_ssid->valuestring != NULL) {
                            ESP_LOGD(TAG, "found WIFI SSID in nvs message");
                            ret = nvs_set_str(hdl_nvs, "SSID", json_wifi_ssid->valuestring);
                            if (ret != ESP_OK) {
                                ESP_LOGE(TAG, "failed to set SSID in NVS namespace WIFI");
                                goto cleanup;
                            }
                        }
                    }

                    nvs_commit(hdl_nvs);

                    ESP_LOGI(TAG, "restarting to apply NVS changes");
                    if (lvgl_port_lock(lvgl_lock_timeout)) {
                        lv_label_set_text_static(lbl_ln3, "NVS updated.");
                        lv_obj_add_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_add_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_add_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_add_flag(lbl_ln5, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_align(lbl_ln3, LV_ALIGN_TOP_MID, 0, 90);
                        lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);
                        lvgl_port_unlock();
                    }
                    reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, true);
                    display_set_backlight(true);
                    restart_delayed();
                }

                cJSON *json_cmd = cJSON_GetObjectItemCaseSensitive(cjson, "cmd");
                if (cJSON_IsString(json_cmd) && json_cmd->valuestring != NULL) {
                    ESP_LOGI(TAG, "found command in WebSocket message: %s", json_cmd->valuestring);
                    if (strcmp(json_cmd->valuestring, "ota_start") == 0) {
                        cJSON *json_ota_url = cJSON_GetObjectItemCaseSensitive(cjson, "ota_url");
                        if (cJSON_IsString(json_ota_url) && json_ota_url->valuestring != NULL) {
                            // we can't pass json_ota_url->valuestring to ota_start
                            // it will be freed before the OTA task reads it
                            char *ota_url = strndup(json_ota_url->valuestring, (strlen(json_ota_url->valuestring)));
                            ESP_LOGI(TAG, "OTA URL: %s", ota_url);
                            ota_start(ota_url);
                        }
                        goto cleanup;
                    }

                    if (strcmp(json_cmd->valuestring, "restart") == 0) {
                        ESP_LOGI(TAG, "restart command received. restart");
                        restart_delayed();
                    }
                }

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
            init_was();
            break;
        default:
            ESP_LOGD(TAG, "unhandled WebSocket event - ID: %d", id_ev);
            break;
    }
}

void was_deinit_task(void *data)
{
    ESP_LOGI(TAG, "stopping WebSocket client");
    esp_err_t ret = esp_websocket_client_destroy(hdl_wc);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to stop WebSocket client: %s", esp_err_to_name(ret));
    }
    vTaskDelete(NULL);
}

void deinit_was(void)
{
    // needs to be done in a task to avoid this error:
    // WEBSOCKET_CLIENT: Client cannot be stopped from websocket task
    xTaskCreate(&was_deinit_task, "was_deinit_task", 4096, NULL, 5, NULL);
}

esp_err_t init_was(void)
{
    const esp_websocket_client_config_t cfg_wc = {
        .buffer_size = 4096,
        .uri = was_url,
        .user_agent = WILLOW_USER_AGENT,
    };
    esp_err_t err = ESP_OK;

    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    ESP_LOGI(TAG, "initializing WebSocket client (%s)", was_url);

    hdl_wc = esp_websocket_client_init(&cfg_wc);
    esp_websocket_register_events(hdl_wc, WEBSOCKET_EVENT_ANY, (esp_event_handler_t)cb_ws_event, NULL);
    err = esp_websocket_client_start(hdl_wc);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start WebSocket client: %s", esp_err_to_name(err));
    }
    return err;
}

void request_config(void)
{
    cJSON *cjson = NULL;
    char *json = NULL;
    esp_err_t ret;

    if (!esp_websocket_client_is_connected(hdl_wc)) {
        esp_websocket_client_destroy(hdl_wc);
        init_was();
    }

    cjson = cJSON_CreateObject();
    if (cJSON_AddStringToObject(cjson, "cmd", "get_config") == NULL) {
        goto cleanup;
    }

    json = cJSON_Print(cjson);

    ret = esp_websocket_client_send_text(hdl_wc, json, strlen(json), 2000 / portTICK_PERIOD_MS);
    cJSON_free(json);
    if (ret < 0) {
        ESP_LOGE(TAG, "failed to send WAS get_config message");
    }

cleanup:
    cJSON_Delete(cjson);
}

static void send_hello(void)
{
    char *json;
    const char *hostname;
    uint8_t mac[6];
    esp_err_t ret;

    if (!esp_websocket_client_is_connected(hdl_wc)) {
        esp_websocket_client_destroy(hdl_wc);
        init_was();
    }

    ret = esp_netif_get_hostname(hdl_netif, &hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to get hostname");
        return;
    }

    ret = esp_efuse_mac_get_default(mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to get MAC address from EFUSE");
        return;
    }

    cJSON *cjson = cJSON_CreateObject();
    cJSON *hello = cJSON_CreateObject();
    cJSON *mac_arr = cJSON_CreateArray();

    for (int i = 0; i < 6; i++) {
        cJSON_AddItemToArray(mac_arr, cJSON_CreateNumber(mac[i]));
    }

    if (cJSON_AddStringToObject(hello, "hostname", hostname) == NULL) {
        goto cleanup;
    }
    if (cJSON_AddStringToObject(hello, "hw_type", str_hw_type(hw_type)) == NULL) {
        goto cleanup;
    }
    if (!cJSON_AddItemToObject(hello, "mac_addr", mac_arr)) {
        goto cleanup;
    }
    if (!cJSON_AddItemToObject(cjson, "hello", hello)) {
        goto cleanup;
    }

    json = cJSON_Print(cjson);

    ret = esp_websocket_client_send_text(hdl_wc, json, strlen(json), 2000 / portTICK_PERIOD_MS);
    cJSON_free(json);
    if (ret < 0) {
        ESP_LOGE(TAG, "failed to send WAS hello message");
    }

cleanup:
    cJSON_Delete(cjson);
}
