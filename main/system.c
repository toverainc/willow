#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "slvgl.h"

static const char *TAG = "WILLOW/SYSTEM";

void restart_delayed(void)
{
    uint32_t delay = esp_random() % 9;
    if (delay < 3) {
        delay = 3;
    } else if (delay > 6) {
        delay = 6;
    }

    ESP_LOGI(TAG, "restarting after %d seconds", delay);

    lvgl_port_lock(0);
    lv_label_set_text_fmt(lbl_ln4, "Restarting in %ds", delay);
    lv_obj_align(lbl_ln4, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();

    delay *= 1000;
    vTaskDelay(delay / portTICK_PERIOD_MS);
    esp_restart();
}
