#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "shared.h"
#include "tasks.h"
#include "timer.h"

esp_timer_handle_t hdl_display_timer = NULL, hdl_sess_timer = NULL;

static void cb_display_timer(void *data)
{
    ESP_LOGI(TAG, "Wake LCD timeout, turning off LCD");
#ifdef CONFIG_ESP32_S3_BOX_LITE_BOARD
    ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 1023, 0);
#else
    ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0, 0);
#endif
}

static void cb_session_timer(void *data)
{
    if (recording) {
        ESP_LOGI(TAG, "session timer expired - forcing end stream");
        int msg = MSG_STOP;
        xQueueSend(q_rec, &msg, 0);
    }
}

esp_err_t init_display_timer(void)
{
    const esp_timer_create_args_t cfg_et = {
        .callback = &cb_display_timer,
        .name = "display_timer",
    };

    return esp_timer_create(&cfg_et, &hdl_display_timer);
}

esp_err_t init_session_timer(void)
{
    const esp_timer_create_args_t cfg_et = {
        .callback = &cb_session_timer,
        .name = "session_timer",
    };

    return esp_timer_create(&cfg_et, &hdl_sess_timer);
}

esp_err_t reset_timer(esp_timer_handle_t hdl, int timeout, bool pause)
{
    if (esp_timer_is_active(hdl)) {
        esp_timer_stop(hdl);
    }
    if (pause) {
        return ESP_OK;
    }
    return esp_timer_start_once(hdl, timeout);
}