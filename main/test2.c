#include "audio_hal.h"
#include "audio_mem.h"
#include "audio_pipeline.h"
#include "audio_recorder.h"
#include "audio_thread.h"
#include "board.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_peripherals.h"
#include "filter_resample.h"
#include "i2s_stream.h"
#include "model_path.h"
#include "raw_stream.h"
#include "recorder_sr.h"

#include "shared.h"

#define I2S_PORT I2S_NUM_0

static const char *TAG = "SALLOW_TEST";

static audio_element_handle_t hdl_ae_rs = NULL;
static audio_rec_handle_t hdl_ar = NULL;
static QueueHandle_t q_rec = NULL;

static esp_err_t cb_ar_event(audio_rec_evt_t are, void *data)
{
    printf("cb_ar_event: ");

    switch(are) {
        case AUDIO_REC_VAD_END:
            printf("AUDIO_REC_VAD_END\n");
            break;
        case AUDIO_REC_VAD_START:
            printf("AUDIO_REC_VAD_START\n");
            break;
        case AUDIO_REC_COMMAND_DECT:
            printf("AUDIO_REC_COMMAND_DECT\n");
            break;
        case AUDIO_REC_WAKEUP_END:
            printf("AUDIO_REC_WAKEUP_END\n");
            break;
        case AUDIO_REC_WAKEUP_START:
            printf("AUDIO_REC_WAKEUP_START\n");
            break;
        default:
            printf("unhandled event: '%d'\n", are);
            break;
    }

    return ESP_OK;
}

static int feed_afe(int16_t *buf, int len, void *ctx, TickType_t ticks)
{
    if (buf == NULL || hdl_ae_rs == NULL) {
        return -1;
    }

    return raw_stream_read(hdl_ae_rs, (char *)buf, len);
}

static void start_rec()
{
    audio_element_handle_t hdl_ae_is, hdl_ae_rf;
    audio_pipeline_cfg_t cfg_ap = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t hdl_ap;

    hdl_ap = audio_pipeline_init(&cfg_ap);
    if (hdl_ap == NULL) {
        return;
    }

    i2s_stream_cfg_t cfg_is = I2S_STREAM_CFG_DEFAULT();
    cfg_is.i2s_config.bits_per_sample = CODEC_ADC_BITS_PER_SAMPLE;
    cfg_is.i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_IRAM;
    cfg_is.i2s_config.sample_rate = 16000;
    cfg_is.i2s_config.use_apll = 0;     // not supported on ESP32-S3-BOX
    cfg_is.i2s_port = CODEC_ADC_I2S_PORT;
    cfg_is.type = AUDIO_STREAM_READER;
    hdl_ae_is = i2s_stream_init(&cfg_is);

#if RESAMPLE
    rsp_filter_cfg_t cfg_rf = DEFAULT_RESAMPLE_FILTER_CONFIG();
    cfg_rf.src_rate = CODEC_ADC_SAMPLE_RATE;
    cfg_rf.dest_rate = 16000;
    hdl_ae_rf = rsp_filter_init(&cfg_rf);

    audio_pipeline_register(hdl_ap, hdl_ae_rf, "rsp_filter");
#endif

    raw_stream_cfg_t cfg_rs = RAW_STREAM_CFG_DEFAULT();
    cfg_rs.type = AUDIO_STREAM_READER;
    hdl_ae_rs = raw_stream_init(&cfg_rs);

    audio_pipeline_register(hdl_ap, hdl_ae_is, "i2s_stream_reader");

    audio_pipeline_register(hdl_ap, hdl_ae_rs, "raw_stream_reader");

#if 0
    const char *tag_link[3] = {"i2s_stream_reader", "rsp_filter", "raw_stream_reader"};
    audio_pipeline_link(hdl_ap, &tag_link[0], 3);
#else
    const char *tag_link[2] = {"i2s_stream_reader", "raw_stream_reader"};
    audio_pipeline_link(hdl_ap, &tag_link[0], 2);
#endif

    audio_pipeline_run(hdl_ap);

    recorder_sr_cfg_t cfg_srr = DEFAULT_RECORDER_SR_CFG();
    cfg_srr.multinet_init = false;

    audio_rec_cfg_t cfg_ar = AUDIO_RECORDER_DEFAULT_CFG();
    cfg_ar.read = (recorder_data_read_t)&feed_afe;
    cfg_ar.sr_handle = recorder_sr_create(&cfg_srr, &cfg_ar.sr_iface);
    cfg_ar.event_cb = cb_ar_event;

    hdl_ar = audio_recorder_create(&cfg_ar);
}

static void at_read(void *data)
{
    const int len = 2 * 1024;
    uint8_t *vd = audio_calloc(1, len);
    int msg = 0;
    TickType_t delay = portMAX_DELAY;

    while (true) {
        if (xQueueReceive(q_rec, &msg, delay) == pdTRUE) {
            printf("at_read() - msg: %d\n", msg);
        }
    }
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_VERBOSE);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    esp_err_t ret;

    esp_periph_config_t pcfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    hdl_pset = esp_periph_set_init(&pcfg);

    audio_board_handle_t hdl_audio_board = audio_board_init();
    //gpio_set_level(get_pa_enable_gpio(), 0);
    ret = audio_hal_ctrl_codec(hdl_audio_board->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    ESP_LOGI(TAG, "audio_hal_ctrl_codec: %s", esp_err_to_name(ret));


    start_rec();

    ESP_LOGI(TAG, "app_main() - start_rec() finished");

    q_rec = xQueueCreate(3, sizeof(int));
    audio_thread_create(NULL, "at_read", at_read, NULL, 4 * 1024, 5, true, 0);
}