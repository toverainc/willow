#include "esp_afe_sr_models.h"

int flag_listen;

extern esp_afe_sr_data_t *data_afe;
extern esp_afe_sr_iface_t *if_afe;

void task_detect(void *arg);
void task_listen(void *arg);