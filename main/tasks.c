#include <stdbool.h>
#include <stddef.h>

#include "driver/i2s.h"
#include "esp_err.h"

#include "tasks.h"

#define I2S_CHANNEL 4

esp_afe_sr_data_t *data_afe = NULL;
esp_afe_sr_iface_t *if_afe = NULL;

static esp_err_t get_i2s_data(int16_t *buf, int len)
{
    esp_err_t ret = ESP_OK;
    int csize = len / (sizeof(int16_t) * I2S_CHANNEL);
    int16_t tmp = 0;
    size_t read;

    ret = i2s_read(I2S_NUM_0, buf, len, &read, portMAX_DELAY);

    for (int i = 0; i < csize; i++) {
        tmp = buf[4 * i + 0];
        buf[3 * i + 0] = buf[4 * i + 1];
        buf[3 * i + 1] = buf[4 * i + 3];
        buf[3 * i + 2] = tmp;
    }

    return ret;
}

void task_detect(void *arg)
{
    esp_afe_sr_data_t *data_afe = arg;
    int csize;
    int16_t *buf_i2s = NULL;

    if (data_afe == NULL) {
        printf("data_afe is NULL\n");
        goto delete;
    }

    if (if_afe == NULL) {
        printf("if_afe is NULL\n");
        goto delete;
    }

    buf_i2s = malloc(csize * sizeof(int16_t));
    csize = if_afe->get_fetch_chunksize(data_afe);

    while (flag_listen) {
        afe_fetch_result_t* res = if_afe->fetch(data_afe);

        if (!res || res->ret_value != ESP_OK) {
            printf("task_detect: failed to fetch audio\n");
        }

        if (res->wakeup_state == WAKENET_DETECTED) {
            printf("task_detect: detected wakeword\n");
        }
    }

    free(buf_i2s);
delete:
    vTaskDelete(NULL);
}

void task_listen(void *arg)
{
    esp_afe_sr_data_t *data_afe = arg;
    int channels, csize;
    int16_t *buf_i2s = NULL;

    if (data_afe == NULL) {
        printf("data_afe is NULL\n");
        goto delete;
    }

    if (if_afe == NULL) {
        printf("if_afe is NULL\n");
        goto delete;
    }

    channels = if_afe->get_channel_num(data_afe);
    csize = if_afe->get_feed_chunksize(data_afe);
    buf_i2s = malloc(csize * sizeof(int16_t) * I2S_CHANNEL);

    while (flag_listen) {
        get_i2s_data(buf_i2s, csize * sizeof(int16_t) * I2S_CHANNEL);
        if_afe->feed(data_afe, buf_i2s);
    }

    free(buf_i2s);
delete:
    vTaskDelete(NULL);
}