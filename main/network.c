#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "lvgl.h"
#include "periph_wifi.h"
#include "sdkconfig.h"

#include "shared.h"
#include "slvgl.h"

#define MAC_ADDR_SIZE 6

uint8_t mac_address[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

void cb_sntp(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP client synchronized time to %lu", tv->tv_sec);
}

esp_err_t init_sntp(void)
{
    ESP_LOGI(TAG, "initializing SNTP client");
    setenv("TZ", CONFIG_WILLOW_TIMEZONE, 1);
    tzset();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
#ifdef CONFIG_WILLOW_NTP_USE_DHCP
    ESP_LOGI(TAG, "Using DHCP SNTP server");
    sntp_servermode_dhcp(1);
#else
    ESP_LOGI(TAG, "Using configured SNTP server '%s'", CONFIG_WILLOW_NTP_HOST);
    sntp_setservername(0, CONFIG_WILLOW_NTP_HOST);
#endif
    sntp_set_time_sync_notification_cb(cb_sntp);
    sntp_init();

    return ESP_OK;
}

#ifndef CONFIG_WILLOW_ETHERNET
esp_err_t init_wifi(void)
{
    esp_err_t ret = ESP_OK;
    periph_wifi_cfg_t cfg_pwifi = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t hdl_pwifi = periph_wifi_init(&cfg_pwifi);

    // Start wifi
    lvgl_port_lock(0);
    lv_label_set_text_static(lbl_ln4, "Connecting to Wi-Fi ...");
    lvgl_port_unlock();

    esp_periph_start(hdl_pset, hdl_pwifi);
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