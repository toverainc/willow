#include "esp_log.h"
#include "esp_transport_ws.h"
#include "esp_websocket_client.h"

static const char *TAG = "WILLOW/WAS";
static esp_websocket_client_handle_t hdl_wc = NULL;

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
                free(resp);
            }
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket disconnected");
            break;
        case WEBSOCKET_EVENT_CLOSED:
            ESP_LOGI(TAG, "WebSocket closed");
            break;
        default:
            ESP_LOGD(TAG, "unhandled WebSocket event - ID: %d", id_ev);
            break;
    }
}

esp_err_t init_was(void)
{
    const esp_websocket_client_config_t cfg_wc = {
        .buffer_size = 4096,
        .uri = CONFIG_WILLOW_WAS_URL,
        .user_agent = WILLOW_USER_AGENT,
    };
    esp_err_t err = ESP_OK;

    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    ESP_LOGI(TAG, "initializing WebSocket client");

    hdl_wc = esp_websocket_client_init(&cfg_wc);
    esp_websocket_register_events(hdl_wc, WEBSOCKET_EVENT_ANY, (esp_event_handler_t)cb_ws_event, NULL);
    err = esp_websocket_client_start(hdl_wc);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start WebSocket client: %s", esp_err_to_name(err));
    }
    return err;
}