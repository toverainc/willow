#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>

#include "audio_hal.h"
#include "audio_pipeline.h"
#include "board.h"
#include "driver/i2s.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "i2s_stream.h"
#include "model_path.h"
#include "spiffs_stream.h"
#include "wav_decoder.h"

#include "i2s.h"
#include "shared.h"
#include "tasks.h"

#define I2S_CHANNEL 4


esp_afe_sr_data_t *data_afe = NULL;
esp_afe_sr_iface_t *if_afe = NULL;

static const char *TAG = "SALLOW_TASKS";

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

void start_wwd_tasks(void)
{
    printf("starting wake word detection tasks\n");

    init_i2s();

    flag_listen = 1;

    if (xTaskCreatePinnedToCore(&task_listen, "listen", 8 * 1024, (void*)data_afe, 5, NULL, 0) != pdPASS) {
        printf("%s: failed to start task_listen\n", TAG);
    }
    if (xTaskCreatePinnedToCore(&task_detect, "detect", 4 * 1024, (void*)data_afe, 5, NULL, 1) != pdPASS) {
        printf("%s: failed to start task_detect\n", TAG);
    }
}

void task_detect(void *arg)
{
    esp_afe_sr_data_t *data_afe = arg;
    int csize;
    int16_t *buf_i2s = NULL;

    if (data_afe == NULL) {
        printf("%s: data_afe is NULL\n", TAG);
        goto delete;
    }

    if (if_afe == NULL) {
        printf("%s: if_afe is NULL\n", TAG);
        goto delete;
    }

    csize = if_afe->get_fetch_chunksize(data_afe);
    buf_i2s = malloc(csize * sizeof(int16_t));

    while (flag_listen) {
        afe_fetch_result_t* res = if_afe->fetch(data_afe);

        if (!res || res->ret_value != ESP_OK) {
            printf("%s: task_detect: failed to fetch audio\n", TAG);
        }

        if (res->wakeup_state == WAKENET_DETECTED) {
            printf("task_detect: detected wakeword\n");
            flag_listen = 0;
            xTaskCreatePinnedToCore(&task_play_spiffs, "play_spiffs", 4 * 1024, (void*)NULL, 5, NULL, 0);
        }
    }

    free(buf_i2s);
delete:
    printf("task_detect delete\n");
    vTaskDelete(NULL);
}

void task_listen(void *arg)
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

    csize = if_afe->get_feed_chunksize(data_afe);
    buf_i2s = malloc(csize * sizeof(int16_t) * I2S_CHANNEL);

    while (flag_listen) {
        get_i2s_data(buf_i2s, csize * sizeof(int16_t) * I2S_CHANNEL);
        if_afe->feed(data_afe, buf_i2s);
    }

    free(buf_i2s);
delete:
    printf("task_listen delete\n");
    vTaskDelete(NULL);
}


void task_play_spiffs(void *arg)
{
    printf("task_play_spiffs()\n");

    DIR *dir = opendir("/spiffs/audio");
    if (dir == NULL) {
        printf("failed to open /spiffs/audio");
    } else {
        while (true) {
            struct dirent *de = readdir(dir);

            if (!de) {
                break;
            }

            printf("found file: %s\n", de->d_name);
        }
    }

    audio_element_handle_t hdl_ae_is, hdl_ae_ss, hdl_ae_wd;
    audio_pipeline_cfg_t cfg_ap = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t hdl_ap;
    
    hdl_ap = audio_pipeline_init(&cfg_ap);
    if (hdl_ap == NULL) {
        goto end;
    }

    spiffs_stream_cfg_t cfg_ss = SPIFFS_STREAM_CFG_DEFAULT();
    cfg_ss.type = AUDIO_STREAM_READER;
    hdl_ae_ss = spiffs_stream_init(&cfg_ss);

    i2s_stream_cfg_t cfg_i2ss = I2S_STREAM_CFG_DEFAULT();
    cfg_i2ss.type = AUDIO_STREAM_WRITER;
    hdl_ae_is = i2s_stream_init(&cfg_i2ss);

    wav_decoder_cfg_t cfg_wd = DEFAULT_WAV_DECODER_CONFIG();
    hdl_ae_wd = wav_decoder_init(&cfg_wd);

    audio_pipeline_register(hdl_ap, hdl_ae_ss, "spiffs");
    audio_pipeline_register(hdl_ap, hdl_ae_wd, "wav");
    audio_pipeline_register(hdl_ap, hdl_ae_is, "i2s");

    const char *link_tag[3] =  {"spiffs", "wav", "i2s"};
    audio_pipeline_link(hdl_ap, &link_tag[0], 3);

    audio_element_set_uri(hdl_ae_ss, "/spiffs/audio/wake.wav");

    audio_event_iface_cfg_t cfg_if_ae = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t hdl_if_ae = audio_event_iface_init(&cfg_if_ae);

    audio_pipeline_set_listener(hdl_ap, hdl_if_ae);
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(hdl_pset), hdl_if_ae);

    gpio_set_level(get_pa_enable_gpio(), 1);
    audio_pipeline_run(hdl_ap);

    while(true) {
        audio_event_iface_msg_t msg_if_ae;
        esp_err_t ret = audio_event_iface_listen(hdl_if_ae, &msg_if_ae, portMAX_DELAY);

        if (ret != ESP_OK) {
            printf("%s: audio event interface error: %s\n", TAG, esp_err_to_name(ret));
            continue;
        }

        if (msg_if_ae.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
            msg_if_ae.source == (void *) hdl_ae_wd &&
            msg_if_ae.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            
                audio_element_info_t inf_ae = {0};
                audio_element_getinfo(hdl_ae_wd, &inf_ae);
                printf("%s: received event from wav decoder: bits='%d', channels='%d', sample_rates='%d'\n",
                        TAG, inf_ae.bits, inf_ae.channels, inf_ae.sample_rates);
                i2s_stream_set_clk(hdl_ae_is, inf_ae.sample_rates, inf_ae.bits, inf_ae.channels);
                continue;
        }

        if (msg_if_ae.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
            msg_if_ae.source == (void *) hdl_ae_wd &&
            msg_if_ae.cmd == AEL_MSG_CMD_REPORT_STATUS &&
            (
                ((int)msg_if_ae.data == AEL_STATUS_STATE_STOPPED) ||
                ((int)msg_if_ae.data == AEL_STATUS_STATE_FINISHED)
            )
        ) {
            break;
        }    
    }

    gpio_set_level(get_pa_enable_gpio(), 0);

    audio_pipeline_wait_for_stop(hdl_ap);
    audio_pipeline_terminate(hdl_ap);
    audio_pipeline_unregister(hdl_ap, hdl_ae_ss);
    audio_pipeline_unregister(hdl_ap, hdl_ae_wd);
    audio_pipeline_unregister(hdl_ap, hdl_ae_is);
    audio_pipeline_remove_listener(hdl_ap);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(hdl_pset), hdl_if_ae);
    audio_event_iface_destroy(hdl_if_ae);
    audio_element_deinit(hdl_ae_ss);
    audio_element_deinit(hdl_ae_is);
    audio_element_deinit(hdl_ae_wd);
    // this deinits I2S
    audio_pipeline_deinit(hdl_ap);

end:
    printf("%s: task_play_spiffs end\n", TAG);
    flag_listen = 1;
    start_wwd_tasks();
    printf("%s: task_play_spiffs delete\n", TAG);
    vTaskDelete(NULL);
}