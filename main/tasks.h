#include "esp_afe_sr_models.h"

struct tasks_t {
    TaskHandle_t detect;
    TaskHandle_t listen;
    TaskHandle_t play;
};

int flag_listen;

extern esp_afe_sr_data_t *data_afe;
extern esp_afe_sr_iface_t *if_afe;

extern struct tasks_t tasks;

void start_wwd_tasks(void);
void task_detect(void *arg);
void task_listen(void *arg);
void task_play_spiffs(void *arg);