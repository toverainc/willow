#include "config.h"
#include "esp_log.h"

#define WILLOW_LOG_LEVEL ESP_LOG_INFO

void init_logging(void)
{
    if (!config_get_bool("debug_log")) {
        esp_log_level_set("*", ESP_LOG_ERROR);
        esp_log_level_set("PERIPH_WIFI", ESP_LOG_WARN);

        esp_log_level_set("WILLOW/AUDIO", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/CONFIG", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/DISPLAY", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/ETHERNET", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/HASS", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/HTTP", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/INPUT", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/LVGL", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/MAIN", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/NETWORK", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/OPENHAB", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/OTA", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/REST", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/SYSTEM", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/TIMER", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/UI", WILLOW_LOG_LEVEL);
        esp_log_level_set("WILLOW/WAS", WILLOW_LOG_LEVEL);
    } else {
        ESP_LOGI("WILLOW", "Debug logging enabled");
    }
}