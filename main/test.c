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
#define WAKENET_PARTLABEL "model"

static const char *TAG = "SALLOW_TEST";

static void init_afe_data(void)
{
    if_afe = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
    afe_config_t cfg_afe = AFE_CONFIG_DEFAULT();
    cfg_afe.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    cfg_afe.wakenet_model_name = WAKENET_NAME;
    data_afe = if_afe->create_from_config(&cfg_afe);

    printf("%s: if_afe: '%p'\n", TAG, if_afe);
}

static esp_err_t init_sr_model()
{
    char *wakenet_name = WAKENET_NAME;
    esp_err_t ret = ESP_OK;
    srmodel_list_t *sr_models = esp_srmodel_init(WAKENET_PARTLABEL);

    printf("found '%d' SR model on SPIFFS\n", sr_models->num);

    if (sr_models != NULL) {
        for (int i = 0; i < sr_models->num; i++) {
            printf("%s: model: %s", TAG, sr_models->model_name[i]);
        }
    }

    if (esp_srmodel_exists(sr_models, wakenet_name) < 0) {
        ret = ESP_ERR_NOT_FOUND;
    }

    return ret;
}

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
    gpio_set_level(get_pa_enable_gpio(), 0);
    ret = audio_hal_ctrl_codec(hdl_audio_board->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    ESP_LOGI(TAG, "audio_hal_ctrl_codec: %s", esp_err_to_name(ret));

    init_sr_model();
    init_afe_data();

    start_wwd_tasks();


    // this works
    // task_play_spiffs("foo");
    // this works too
    // xTaskCreate(&task_play_spiffs, "play_spiffs", 4 * 1024, (void*)NULL, 5, NULL);
}