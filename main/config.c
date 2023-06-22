#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "lvgl.h"

#include "config.h"
#include "display.h"
#include "slvgl.h"
#include "system.h"
#include "timer.h"
#include "was.h"

#define CONFIG_PATH "/spiffs/user/config/willow.json"

static const char *TAG = "WILLOW/CONFIG";

bool config_valid = false;
cJSON *wc = NULL; // TODO: cJSON_Delete() after all config_get_* calls

static char *config_read(void)
{
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

bool config_get_bool(char *key)
{
    bool ret = false;
    cJSON *val = cJSON_GetObjectItemCaseSensitive(wc, key);
    if (cJSON_IsBool(val)) {
        ret = cJSON_IsTrue(val) ? true : false;
    }
    return ret;
}

char *config_get_char(char *key)
{
    char *ret = NULL;
    cJSON *val = cJSON_GetObjectItemCaseSensitive(wc, key);
    if (cJSON_IsString(val) && val->valuestring != NULL) {
        ret = strndup(val->valuestring, strlen(val->valuestring));
    } else {
        ESP_LOGW(TAG, "key %s not found in config, use bogus value to avoid NULL pointer dereference", key);
        ret = strndup("bogus", 6);
    }
    return ret;
}

int config_get_int(char *key)
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
    lvgl_port_lock(0);
    lv_label_set_text_static(lbl_ln3, "Config updated.");
    lv_obj_add_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lbl_ln5, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(lbl_ln3, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
    reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, true);
    display_set_backlight(true);
    restart_delayed();
}
