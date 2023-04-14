#include <stdio.h>

#include "board.h"
#include "driver/i2s.h"
#include "esp_log.h"
#include "esp_peripherals.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_path.h"

#include "tasks.h"

#define I2S_PORT I2S_NUM_0
#define WAKENET_NAME "wn9_hiesp"
#define WAKENET_PARTLABEL "model"

static const char *TAG = "SALLOW";

static void init_afe_data(void)
{
    if_afe = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
    afe_config_t cfg_afe = AFE_CONFIG_DEFAULT();
    cfg_afe.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    cfg_afe.wakenet_model_name = WAKENET_NAME;
    data_afe = if_afe->create_from_config(&cfg_afe);

    ESP_LOGI(TAG, "if_afe: '%p'", if_afe);
}

static esp_err_t init_i2s(void)
{
    esp_err_t ret = ESP_OK;
    i2s_config_t cfg_i2s = {
        .bits_per_chan          = I2S_BITS_PER_CHAN_32BIT,
        .bits_per_sample        = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format         = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format   = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count          = 6,
        .dma_buf_len            = 160,
        .fixed_mclk             = 0,
        .intr_alloc_flags       = ESP_INTR_FLAG_LEVEL1,
        .mclk_multiple          = I2S_MCLK_MULTIPLE_DEFAULT,
        .mode                   = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX,
        .sample_rate            = 16000,
        .tx_desc_auto_clear     = true,
        .use_apll               = false,
    };

    i2s_pin_config_t pcfg_i2s = {
        .bck_io_num = GPIO_NUM_17,
        .ws_io_num = GPIO_NUM_47,
        .data_out_num = GPIO_NUM_15,
        .data_in_num = GPIO_NUM_16,
        .mck_io_num = GPIO_NUM_2,
    };

    ret = i2s_driver_install(I2S_PORT, &cfg_i2s, 0, NULL);
    ESP_LOGI(TAG, "i2s_driver_install: %s", esp_err_to_name(ret));

    ret = i2s_set_pin(I2S_PORT, &pcfg_i2s);
    ESP_LOGI(TAG, "i2s_set_pin: %s", esp_err_to_name(ret));

    return ret;
}

static esp_err_t init_sr_model()
{
    char *wakenet_name = WAKENET_NAME;
    esp_err_t ret = ESP_OK;
    srmodel_list_t *sr_models = esp_srmodel_init(WAKENET_PARTLABEL);

    ESP_LOGD(TAG, "found '%d' SR model on SPIFFS", sr_models->num);

    if (sr_models != NULL) {
        for (int i = 0; i < sr_models->num; i++) {
            ESP_LOGD(TAG, "model: %s", sr_models->model_name[i]);
        }
    }

    if (esp_srmodel_exists(sr_models, wakenet_name) < 0) {
        ret = ESP_ERR_NOT_FOUND;
    }

    return ret;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_DEBUG);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    esp_err_t ret;

    audio_board_handle_t hdl_audio_board = audio_board_init();
    ret = audio_hal_ctrl_codec(hdl_audio_board->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    init_i2s();
    init_sr_model();
    init_afe_data();

    flag_listen = 1;
    xTaskCreatePinnedToCore(&task_listen, "listen", 8 * 1024, (void*)data_afe, 5, NULL, 0);
    xTaskCreatePinnedToCore(&task_detect, "detect", 4 * 1024, (void*)data_afe, 5, NULL, 1);
}