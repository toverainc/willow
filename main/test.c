#include "audio_hal.h"
#include "board.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_peripherals.h"
#include "esp_spiffs.h"
#include "model_path.h"
#include "periph_spiffs.h"

#include "i2s.h"
#include "shared.h"
#include "tasks.h"

#define PARTLABEL_AUDIO "audio"


static const char *TAG = "SALLOW_TEST";

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_VERBOSE);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    esp_err_t ret;

    esp_periph_config_t pcfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    hdl_pset = esp_periph_set_init(&pcfg);

    periph_spiffs_cfg_t pcfg_spiffs = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .partition_label = PARTLABEL_AUDIO,
        .root = "/spiffs/audio",
    };
    esp_periph_handle_t phdl_spiffs = periph_spiffs_init(&pcfg_spiffs);
    ret = esp_periph_start(hdl_pset, phdl_spiffs);
    ESP_LOGI(TAG, "esp_periph_start: %s", esp_err_to_name(ret));

    while (!periph_spiffs_is_mounted(phdl_spiffs)) {
        ESP_LOGI(TAG, "periph_spiffs_is_mounted");
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    audio_board_handle_t hdl_audio_board = audio_board_init();
    ret = audio_hal_ctrl_codec(hdl_audio_board->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    ESP_LOGI(TAG, "audio_hal_ctrl_codec: %s", esp_err_to_name(ret));



    start_wwd_tasks();


    // this works
    // task_play_spiffs("foo");
    // this works too
    // xTaskCreate(&task_play_spiffs, "play_spiffs", 4 * 1024, (void*)NULL, 5, NULL);
}