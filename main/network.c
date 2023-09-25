#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "lvgl.h"
#include "lwip/ip_addr.h"
#include "periph_wifi.h"
#include "sdkconfig.h"

#include "config.h"
#include "network.h"
#include "shared.h"
#include "slvgl.h"
#include "system.h"

#define DEFAULT_NTP_CONFIG "Host"
#define DEFAULT_NTP_HOST   "pool.ntp.org"
#define DEFAULT_TIMEZONE   "America/Menominee"

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif

#define HOSTNAME_SIZE 20
#define MAC_ADDR_SIZE 6

static const char *TAG = "WILLOW/NETWORK";
uint8_t mac_address[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

void cb_sntp(struct timeval *tv)
{
    for (uint8_t i = 0; i < CONFIG_LWIP_SNTP_MAX_SERVERS; ++i) {
        if (esp_sntp_getservername(i)) {
            ESP_LOGI(TAG, "SNTP client synchronized time to %lld from server %s", tv->tv_sec,
                     esp_sntp_getservername(i));
        } else {
            // we have either IPv4 or IPv6 address, let's print it
            char buff[INET6_ADDRSTRLEN];
            ip_addr_t const *ip = esp_sntp_getserver(i);
            if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL) {
                ESP_LOGI(TAG, "SNTP client synchronized time to %lld from server %s", tv->tv_sec, buff);
            }
        }
    }
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

// IDF5 WIP: Does not actually apply custom dynamic config values
esp_err_t init_sntp(void)
{
    ESP_LOGI(TAG, "initializing SNTP client");
    char *timezone = config_get_char("timezone", DEFAULT_TIMEZONE);
    setenv("TZ", timezone, 1);
    free(timezone);
    tzset();

    esp_sntp_config_t esp_sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(DEFAULT_NTP_HOST);
    esp_sntp_config.sync_cb = cb_sntp;
    esp_sntp_config.index_of_first_server = 0;
    esp_sntp_config.server_from_dhcp = false;
    esp_sntp_config.start = false;

    char *ntp_config = config_get_char("ntp_config", DEFAULT_NTP_CONFIG);
    if (strcmp(ntp_config, "DHCP") == 0) {
        ESP_LOGI(TAG, "Using DHCP SNTP server");
        esp_sntp_servermode_dhcp(1);
        esp_sntp_config.server_from_dhcp = true;
        esp_sntp_config.renew_servers_after_new_IP = true;
#ifdef CONFIG_WILLOW_ETHERNET
        esp_sntp_config.ip_event_to_renew = IP_EVENT_ETH_GOT_IP;
#else
        esp_sntp_config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
#endif
    } else if (strcmp(ntp_config, "Host") == 0) {
        char *ntp_host = config_get_char("ntp_host", DEFAULT_NTP_HOST);
        ESP_LOGI(TAG, "Using configured SNTP server '%s'", ntp_host);
        esp_sntp_setservername(0, ntp_host);
        free(ntp_host);
    }
    free(ntp_config);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_netif_sntp_init(&esp_sntp_config);
    esp_netif_sntp_start();

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
    if (lvgl_port_lock(lvgl_lock_timeout)) {
        lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_static(lbl_ln4, "Connecting to Wi-Fi ...");
        lvgl_port_unlock();
    }

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
