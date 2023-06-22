#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"

#include "config.h"
#include "system.h"
#include "was.h"

#define CONFIG_PATH "/spiffs/user/config/willow.json"

static const char *TAG = "WILLOW/CONFIG";

bool config_valid = false;
cJSON *wc = NULL; // TODO: cJSON_Delete() after all config_get_* calls

static char *config_read(void)
{
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    char *config = NULL;

    ESP_LOGI(TAG, "opening %s", CONFIG_PATH);

    FILE *f = fopen(CONFIG_PATH, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "failed to open %s", CONFIG_PATH);
        goto close;
    }

    struct stat fs;
    if (stat(CONFIG_PATH, &fs)) {
        ESP_LOGE(TAG, "failed to get file status");
        goto close;
    }
    ESP_LOGI(TAG, "config file size: %ld", fs.st_size);

    config = calloc(sizeof(char), fs.st_size + 1);
    size_t rlen = fread(config, 1, fs.st_size, f);
    ESP_LOGI(TAG, "fread: %d", rlen);
    config[fs.st_size] = '\0';
    ESP_LOGI(TAG, "config file content: %s", config);
close:
    fclose(f);

    return config;
}

bool config_get_bool(char *key, const bool default_value)
{
    bool ret = default_value;
    cJSON *val = cJSON_GetObjectItemCaseSensitive(wc, key);
    if (val != NULL && cJSON_IsBool(val)) {
        ret = cJSON_IsTrue(val) ? true : false;
    }
    return ret;
}

char *config_get_char(const char *key, const char *default_value)
{
    char *ret = NULL;
    cJSON *val = cJSON_GetObjectItemCaseSensitive(wc, key);
    if (val != NULL && cJSON_IsString(val) && val->valuestring != NULL) {
        ret = strndup(val->valuestring, strlen(val->valuestring));
    } else {
        ret = strndup(default_value, strlen(default_value));
    }
    return ret;
}

int config_get_int(char *key, const int default_value)
{
    int ret = -1;
    cJSON *val = cJSON_GetObjectItemCaseSensitive(wc, key);
    if (cJSON_IsNumber(val)) {
        ret = val->valueint;
    }
    return ret;
}

void config_parse(void)
{
    char *config = config_read();
    char *json = NULL;

    if (config == NULL) {
        return;
    }

    wc = cJSON_Parse(config);
    if (wc == NULL) {
        const char *eptr = cJSON_GetErrorPtr();
        if (eptr != NULL) {
            ESP_LOGE(TAG, "error parsing config file: %s\n", eptr);
            goto cleanup;
        }
    }

    config_valid = true;

    json = cJSON_Print(wc);
    ESP_LOGI(TAG, "parsed config file:");
    printf("%s\n", json);

cleanup:
    free(config);
}

void config_write(const char *data)
{
    FILE *f = fopen(CONFIG_PATH, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "failed to open %s", CONFIG_PATH);
        goto close;
    }
    fputs(data, f);

close:
    fclose(f);

    ESP_LOGI(TAG, "%s updated, restarting", CONFIG_PATH);
    restart_delayed();
}
