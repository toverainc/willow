#include "cJSON.h"
#include "esp_log.h"
#include "esp_transport_ws.h"
#include "esp_websocket_client.h"

#include "config.h"
#include "network.h"
#include "ota.h"
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
                    goto cleanup;
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

    ESP_LOGI(TAG, "initializing WebSocket client");

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

    cJSON *cjson = cJSON_CreateObject();
    cJSON *hello = cJSON_CreateObject();
    if (cJSON_AddStringToObject(hello, "hostname", hostname) == NULL) {
        goto cleanup;
    }
    if (!cJSON_AddItemToObject(cjson, "hello", hello)) {
        goto cleanup;
    }

    json = cJSON_Print(cjson);

    ret = esp_websocket_client_send_text(hdl_wc, json, strlen(json), 2000 / portTICK_PERIOD_MS);
    if (ret < 0) {
        ESP_LOGE(TAG, "failed to send WAS hello message");
    }

cleanup:
    cJSON_Delete(cjson);
}
