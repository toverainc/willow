esp_netif_t *hdl_netif;

esp_err_t init_sntp(void);
esp_err_t init_wifi(void);
esp_err_t init_ethernet(void);
void get_mac_address(void);