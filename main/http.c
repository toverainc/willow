#include "esp_http_client.h"
#include "esp_log.h"

#include "shared.h"

static const char *TAG = "WILLOW/HTTP";

esp_http_client_handle_t init_http_client(void)
{
    esp_http_client_config_t cfg_hc = {
        // either host and path or url should be set
        .url = "http://dummy",
        .user_agent = WILLOW_USER_AGENT,
    };

    return esp_http_client_init(&cfg_hc);
}

static esp_err_t http_do(const esp_http_client_handle_t hdl_hc, const esp_http_client_method_t method, const char *url,
                         const char *ctype, const char *data, char **body, int *http_status)
{
    esp_err_t ret;
    int n;

    esp_http_client_set_url(hdl_hc, url);
    esp_http_client_set_method(hdl_hc, method);
    if (ctype != NULL) {
        esp_http_client_set_header(hdl_hc, "Content-Type", ctype);
    }
    ret = esp_http_client_open(hdl_hc, method == HTTP_METHOD_POST ? strlen(data) : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to open HTTP connection: %s", esp_err_to_name(ret));
        return ret;
    }
    if (method == HTTP_METHOD_POST) {
        ESP_LOGV(TAG, "sending '%s' to '%s'", data, url);
        n = esp_http_client_write(hdl_hc, data, strlen(data));
        if (n < 0) {
            ESP_LOGE(TAG, "failed to POST HTTP data");
            return ESP_FAIL;
        }
    }
    n = esp_http_client_fetch_headers(hdl_hc);
    if (n < 0) {
        ESP_LOGE(TAG, "failed to get HTTP headers");
        return ESP_FAIL;
    }
    *body = calloc(sizeof(char), n + 1);
    n = esp_http_client_read_response(hdl_hc, *body, n);
    if (n >= 0) {
        *http_status = esp_http_client_get_status_code(hdl_hc);
        ESP_LOGI(TAG, "HTTP status='%d' content_length='%d'", *http_status, esp_http_client_get_content_length(hdl_hc));
    }
    esp_http_client_cleanup(hdl_hc);

    return ret;
}

esp_err_t http_get(const esp_http_client_handle_t hdl_hc, const char *url, char **body, int *http_status)
{
    return http_do(hdl_hc, HTTP_METHOD_GET, url, NULL, NULL, body, http_status);
}

esp_err_t http_post(const esp_http_client_handle_t hdl_hc, const char *url, const char *ctype, const char *data,
                    char **body, int *http_status)
{
    return http_do(hdl_hc, HTTP_METHOD_POST, url, ctype, data, body, http_status);
}

esp_err_t http_set_basic_auth(const esp_http_client_handle_t hdl_hc, const char *username, const char *password)
{
    esp_err_t ret = ESP_OK;
    ret = esp_http_client_set_authtype(hdl_hc, HTTP_AUTH_TYPE_BASIC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to enable HTTP Basic Authentication: %s", esp_err_to_name(ret));
    }
    ret = esp_http_client_set_username(hdl_hc, username);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set HTTP username: %s", esp_err_to_name(ret));
    }
    ret = esp_http_client_set_password(hdl_hc, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set HTTP password: %s", esp_err_to_name(ret));
    }
    return ret;
}
