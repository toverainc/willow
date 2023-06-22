#include "board.h"
#include "esp_decoder.h"
#include "esp_log.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "sdkconfig.h"
#include "spiffs_stream.h"

#include "config.h"
#include "shared.h"

#define WIS_URL_TTS_ARG "?speaker=CLB&text="

static const char *TAG = "WILLOW/AUDIO";

static void play_audio_err(void *data)
{
    gpio_set_level(get_pa_enable_gpio(), 1);
    esp_audio_sync_play(hdl_ea, "spiffs://spiffs/user/audio/error.flac", 0);
    gpio_set_level(get_pa_enable_gpio(), 0);
}

static void play_audio_ok(void *data)
{
    gpio_set_level(get_pa_enable_gpio(), 1);
    esp_audio_sync_play(hdl_ea, "spiffs://spiffs/user/audio/success.flac", 0);
    gpio_set_level(get_pa_enable_gpio(), 0);
}

static void play_audio_wis_tts(void *data)
{
    if (data == NULL) {
        ESP_LOGW(TAG, "called play_audio_wis_tts with NULL data");
        return;
    }
    int len_url = strlen(config_get_char("wis_tts_url")) + strlen(WIS_URL_TTS_ARG) + strlen((char *)data) + 1;
    char *url = calloc(sizeof(char), len_url);
    snprintf(url, len_url, "%s%s%s", config_get_char("wis_tts_url"), WIS_URL_TTS_ARG, (char *)data);
    gpio_set_level(get_pa_enable_gpio(), 1);
    ESP_LOGI(TAG, "Using WIS TTS URL '%s'", url);
    esp_audio_sync_play(hdl_ea, url, 0);
    ESP_LOGI(TAG, "WIS TTS playback finished");
    gpio_set_level(get_pa_enable_gpio(), 0);
    free(url);
}

static void noop(void *data)
{
}

void init_audio_response(void)
{
    if (strcmp(config_get_char("audio_response_type"), "Chimes") == 0) {
        war.fn_err = play_audio_err;
        war.fn_ok = play_audio_ok;
    } else if (strcmp(config_get_char("audio_response_type"), "TTS") == 0) {
        war.fn_err = play_audio_wis_tts;
        war.fn_ok = play_audio_wis_tts;
    } else {
        war.fn_err = noop;
        war.fn_ok = noop;
    }
}

void init_esp_audio(audio_board_handle_t hdl)
{
    audio_err_t ret = ESP_OK;
    esp_audio_cfg_t cfg_ea = {
        .cb_ctx = NULL,
        .cb_func = NULL,
        .component_select = ESP_AUDIO_COMPONENT_SELECT_DEFAULT,
        .evt_que = NULL,
        .in_stream_buf_size = 10 * 1024,
        .out_stream_buf_size = 4 * 1024,
        .prefer_type = ESP_AUDIO_PREFER_MEM,
        .resample_rate = 16000,
        .task_prio = 6,
        .task_stack = 4 * 1024,
        .vol_get = (audio_volume_get)audio_hal_get_volume,
        .vol_handle = hdl->audio_hal,
        .vol_set = (audio_volume_set)audio_hal_set_volume,
    };

    hdl_ea = esp_audio_create(&cfg_ea);

    http_stream_cfg_t cfg_hs = HTTP_STREAM_CFG_DEFAULT();

    ret = esp_audio_input_stream_add(hdl_ea, http_stream_init(&cfg_hs));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to add HTTP input stream to ESP Audio");
    }

    spiffs_stream_cfg_t cfg_ss = SPIFFS_STREAM_CFG_DEFAULT();
    cfg_ss.type = AUDIO_STREAM_READER;

    ret = esp_audio_input_stream_add(hdl_ea, spiffs_stream_init(&cfg_ss));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to add input stream to ESP Audio");
    }

    audio_decoder_t ad[] = {
        DEFAULT_ESP_FLAC_DECODER_CONFIG(),
    };

    esp_decoder_cfg_t cfg_dec = {
        .out_rb_size = ESP_DECODER_RINGBUFFER_SIZE,
        .plus_enable = false,
        .stack_in_ext = true,
        .task_core = 0,
        .task_prio = ESP_DECODER_TASK_PRIO,
        .task_stack = ESP_DECODER_TASK_STACK_SIZE,
    };

    ret = esp_audio_codec_lib_add(hdl_ea, AUDIO_CODEC_TYPE_DECODER,
                                  esp_decoder_init(&cfg_dec, ad, sizeof(ad) / sizeof(audio_decoder_t)));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to add FLAC decoder to ESP Audio");
    }

    i2s_stream_cfg_t cfg_is = {
        .expand_src_bits = I2S_BITS_PER_SAMPLE_16BIT,
        .i2s_config = {
            .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .dma_buf_count = 3,
            .dma_buf_len = 300,
            .fixed_mclk = 0,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_IRAM,
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
            .sample_rate = 16000,
            .tx_desc_auto_clear = true,
            .use_apll = false, // not supported on ESP32-S3-BOX
        },
        .i2s_port = CODEC_ADC_I2S_PORT,
        .multi_out_num = 0,
        .need_expand = true,
        .out_rb_size = 8 * 1024, // default is 8 * 1024
        .stack_in_ext = false,
        .task_core = I2S_STREAM_TASK_CORE,
        .task_prio = I2S_STREAM_TASK_PRIO,
        .task_stack = I2S_STREAM_TASK_STACK,
        .type = AUDIO_STREAM_WRITER,
        .uninstall_drv = true,
        .use_alc = false,
        .volume = 0,
    };

    ret = esp_audio_output_stream_add(hdl_ea, i2s_stream_init(&cfg_is));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to add output stream to ESP Audio");
    }
    esp_audio_vol_set(hdl_ea, CONFIG_WILLOW_VOLUME);
    ESP_LOGI(TAG, "audio player initialized");
}