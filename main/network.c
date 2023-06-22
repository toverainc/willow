#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "lvgl.h"
#include "periph_wifi.h"
#include "sdkconfig.h"

#include "config.h"
#include "network.h"
#include "shared.h"
#include "slvgl.h"

#define HOSTNAME_SIZE 20
#define MAC_ADDR_SIZE 6

static const char *TAG = "WILLOW/NETWORK";
uint8_t mac_address[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

void cb_sntp(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP client synchronized time to %lu", tv->tv_sec);
}

void set_hostname(esp_mac_type_t emt)
{
    esp_err_t ret = ESP_OK;
    uint8_t mac[MAC_ADDR_SIZE];

    ret = esp_read_mac(mac, emt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to read MAC address, using default hostname (%s)", CONFIG_LWIP_LOCAL_HOSTNAME);
        return;
    }

    while (esp_netif_get_nr_of_ifs() == 0) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    char hostname[HOSTNAME_SIZE];
    hdl_netif = esp_netif_next(NULL);

    snprintf(hostname, HOSTNAME_SIZE, "willow-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);

    ret = esp_netif_set_hostname(hdl_netif, hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set hostname (%s): %s", hostname, esp_err_to_name(ret));
    }
}

esp_err_t init_sntp(void)
{
    ESP_LOGI(TAG, "initializing SNTP client");
    setenv("TZ", CONFIG_WILLOW_TIMEZONE, 1);
    tzset();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);

    if (strcmp(config_get_char("ntp_config"), "DHCP") == 0) {
        ESP_LOGI(TAG, "Using DHCP SNTP server");
        sntp_servermode_dhcp(1);
    } else if (strcmp(config_get_char("ntp_config"), "Host") == 0) {
        ESP_LOGI(TAG, "Using configured SNTP server '%s'", config_get_char("ntp_host"));
        sntp_setservername(0, config_get_char("ntp_host"));
    }
    sntp_set_time_sync_notification_cb(cb_sntp);
    sntp_init();

    return ESP_OK;
}

#ifndef CONFIG_WILLOW_ETHERNET
esp_err_t init_wifi(const char *psk, const char *ssid)
{
    esp_err_t ret = ESP_OK;
    periph_wifi_cfg_t cfg_pwifi = {
        .ssid = ssid,
        .password = psk,
    };
    esp_periph_handle_t hdl_pwifi = periph_wifi_init(&cfg_pwifi);

    // Start wifi
    lvgl_port_lock(0);
    lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_static(lbl_ln4, "Connecting to Wi-Fi ...");
    lvgl_port_unlock();

    esp_periph_start(hdl_pset, hdl_pwifi);
    set_hostname(ESP_MAC_WIFI_STA);
    periph_wifi_wait_for_connected(hdl_pwifi, portMAX_DELAY);

    ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set Wi-Fi power save mode");
    }
    return ret;
}
#endif

void get_mac_address(void)
{
    uint8_t mac[MAC_ADDR_SIZE];
    esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    ESP_LOGI(TAG, "MAC address: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}