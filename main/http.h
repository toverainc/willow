esp_http_client_handle_t init_http_client(void);
esp_err_t http_get(esp_http_client_handle_t hdl_hc, char *url, char **body, int *http_status);
esp_err_t http_post(esp_http_client_handle_t hdl_hc, char *url, char *data, char **body, int *http_status);
esp_err_t http_set_basic_auth(esp_http_client_handle_t hdl_hc, char *username, char *password);