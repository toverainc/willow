#include "esp_log.h"
#include "esp_sntp.h"
#include "sdkconfig.h"

#include "shared.h"

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