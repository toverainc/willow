#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifdef CONFIG_WILLOW_DEBUG_RUNTIME_STATS
void task_debug_runtime_stats(void *data)
{
    char buf[2048];
    while (true) {
        vTaskGetRunTimeStats(buf);
        printf("%s\n", buf);
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}
#endif