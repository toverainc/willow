esp_http_client_handle_t init_http_client(void);
esp_err_t http_get(const esp_http_client_handle_t hdl_hc, const char *url, char **body, int *http_status);
esp_err_t http_post(const esp_http_client_handle_t hdl_hc, const char *url, const char *ctype, const char *data,
                    char **body, int *http_status);
esp_err_t http_set_basic_auth(const esp_http_client_handle_t hdl_hc, const char *username, const char *password);