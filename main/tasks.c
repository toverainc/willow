#include "driver/ledc.h"
#include "driver/timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "shared.h"
#include "timer.h"

void task_timer(void *data)
{
    int value;
    while(true) {
        xQueueReceive(hdl_q_timer, &value, portMAX_DELAY);
        ESP_LOGI(TAG, "received event in timer queue");
        // sleep value seconds
        vTaskDelay(value * 1000 / portTICK_PERIOD_MS);
        ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0, 0);
        timer_pause(TIMER_GROUP_0, TIMER_0);
    }
}