#include "audio_hal.h"
#include "audio_mem.h"
#include "audio_pipeline.h"
#include "audio_recorder.h"
#include "audio_thread.h"
#include "board.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_peripherals.h"
#include "esp_wifi.h"
#include "filter_resample.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "model_path.h"
#include "nvs_flash.h"
#include "periph_wifi.h"
#include "raw_stream.h"
#include "recorder_sr.h"
#include "sdkconfig.h"


#include "shared.h"

#define I2S_PORT I2S_NUM_0

static bool stream_to_api = false;
static const char *TAG = "SALLOW_TEST";
static enum q_msg {
    MSG_STOP,
    MSG_START,
};
static int total_write = 0;

static audio_element_handle_t hdl_ae_hs, hdl_ae_rs_from_i2s, hdl_ae_rs_to_api = NULL;
static audio_pipeline_handle_t hdl_ap_to_api;
static audio_rec_handle_t hdl_ar = NULL;
static QueueHandle_t q_rec = NULL;

const int32_t tone[] = {
    0x00007fff, 0x00007fff,
    0x00000000, 0x00000000,
    0x80008000, 0x80008000,
    0x00000000, 0x00000000,
};

static void play_tone(void *data)
{
    gpio_set_level(get_pa_enable_gpio(), 1);

    size_t bytes_written;
    int64_t start_time = esp_timer_get_time();

    while ((esp_timer_get_time() - start_time) < 500000) {
        int ret = i2s_write(0, tone, sizeof(tone), &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s write failed");
        }
    }

    gpio_set_level(get_pa_enable_gpio(), 0);

    vTaskDelete(NULL);
}

static esp_err_t cb_ar_event(audio_rec_evt_t are, void *data)
{
    printf("cb_ar_event: ");

    int msg = -1;

    switch(are) {
        case AUDIO_REC_VAD_END:
            printf("AUDIO_REC_VAD_END\n");
            msg = MSG_STOP;
            xQueueSend(q_rec, &msg, 0);
            break;
        case AUDIO_REC_VAD_START:
            printf("AUDIO_REC_VAD_START\n");
            msg = MSG_START;
            xQueueSend(q_rec, &msg, 0);
            break;
        case AUDIO_REC_COMMAND_DECT:
            printf("AUDIO_REC_COMMAND_DECT\n");
            break;
        case AUDIO_REC_WAKEUP_END:
            printf("AUDIO_REC_WAKEUP_END\n");
            break;
        case AUDIO_REC_WAKEUP_START:
            printf("AUDIO_REC_WAKEUP_START\n");
            audio_thread_create(NULL, "play_tone", play_tone, NULL, 4 * 1024, 5, true, 0);
            break;
        default:
            printf("unhandled event: '%d'\n", are);
            break;
    }

    return ESP_OK;
}

static int feed_afe(int16_t *buf, int len, void *ctx, TickType_t ticks)
{
    int ret = 0;
    if (buf == NULL || hdl_ae_rs_from_i2s == NULL) {
        return -1;
    }

    return raw_stream_read(hdl_ae_rs_from_i2s, (char *)buf, len);
}

esp_err_t hdl_ev_hs(http_stream_event_msg_t *msg)
{
    if (msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_handle_t http = (esp_http_client_handle_t)msg->http_client;
    char len_buf[16];
    int wlen = 0;

    switch(msg->event_id) {
        case HTTP_STREAM_PRE_REQUEST:
            ESP_LOGI(TAG, "[ + ] HTTP client HTTP_STREAM_PRE_REQUEST, length=%d", msg->buffer_len);
            esp_http_client_set_method(http, HTTP_METHOD_POST);
            char dat[10] = {0};
            snprintf(dat, sizeof(dat), "%d", 16000);
            esp_http_client_set_header(http, "x-audio-sample-rate", dat);
            memset(dat, 0, sizeof(dat));
            snprintf(dat, sizeof(dat), "%d", 16);
            esp_http_client_set_header(http, "x-audio-bits", dat);
            memset(dat, 0, sizeof(dat));
            snprintf(dat, sizeof(dat), "%d", 1);
            esp_http_client_set_header(http, "x-audio-channel", dat);
            total_write = 0;
            return ESP_OK;

        case HTTP_STREAM_ON_REQUEST:
            ESP_LOGI(TAG, "[ + ] HTTP client HTTP_STREAM_ON_REQUEST, length=%d", msg->buffer_len);
            wlen = sprintf(len_buf, "%x\r\n", msg->buffer_len);
            if (esp_http_client_write(http, len_buf, wlen) <= 0) {
                return ESP_FAIL;
            }
            if (esp_http_client_write(http, msg->buffer, msg->buffer_len) <= 0) {
                return ESP_FAIL;
            }
            if (esp_http_client_write(http, "\r\n", 2) <= 0) {
                return ESP_FAIL;
            }
            total_write += msg->buffer_len;
            printf("\033[A\33[2K\rTotal bytes written: %d\n", total_write);
            return msg->buffer_len;

        case HTTP_STREAM_POST_REQUEST:
            ESP_LOGI(TAG, "[ + ] HTTP client HTTP_STREAM_POST_REQUEST, write end chunked marker");
            if (esp_http_client_write(http, "0\r\n\r\n", 5) <= 0) {
                return ESP_FAIL;
            }
            return ESP_OK;

        case HTTP_STREAM_FINISH_REQUEST:
            ESP_LOGI(TAG, "[ + ] HTTP client HTTP_STREAM_FINISH_REQUEST");
            // Allocate memory for response. Should be enough?
            char *buf = malloc(2048);
            assert(buf);
            int read_len = esp_http_client_read(http, buf, 2048);
            if (read_len <= 0) {
                free(buf);
                return ESP_FAIL;
            }
            buf[read_len] = 0;
            ESP_LOGI(TAG, "Got HTTP Response = %s", (char *)buf);

            audio_pipeline_pause(hdl_ap_to_api);

            free(buf);
            return ESP_OK;

        default:
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t init_ap_to_api()
{
    printf("init_ap_to_api()");
    //audio_element_handle_t hdl_ae_hs;
    audio_pipeline_cfg_t cfg_ap = DEFAULT_AUDIO_PIPELINE_CONFIG();
    hdl_ap_to_api = audio_pipeline_init(&cfg_ap);

    http_stream_cfg_t cfg_hs = HTTP_STREAM_CFG_DEFAULT();
    cfg_hs.event_handle = hdl_ev_hs;
    cfg_hs.type = AUDIO_STREAM_WRITER;
    hdl_ae_hs = http_stream_init(&cfg_hs);

    raw_stream_cfg_t cfg_rs = RAW_STREAM_CFG_DEFAULT();
    cfg_rs.type = AUDIO_STREAM_WRITER;
    hdl_ae_rs_to_api = raw_stream_init(&cfg_rs);

    audio_pipeline_register(hdl_ap_to_api, hdl_ae_hs, "http_stream_writer");
    audio_pipeline_register(hdl_ap_to_api, hdl_ae_rs_to_api, "raw_stream_writer_to_api");

    const char *tag_link[2] = {"raw_stream_writer_to_api", "http_stream_writer"};
    audio_pipeline_link(hdl_ap_to_api, &tag_link[0], 2);
    audio_element_set_uri(hdl_ae_hs, CONFIG_SERVER_URI);

    audio_element_info_t info = AUDIO_ELEMENT_INFO_DEFAULT();
    audio_element_getinfo(hdl_ae_hs, &info);
    ESP_LOGI(TAG, "audio_element_getinfo(hdl_ae_hs): sample_rate='%d' channels='%d' bits='%d' bps = '%d'",
             info.sample_rates, info.channels, info.bits, info.bps);

    return ESP_OK;
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
    cfg_is.i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_IRAM;
    cfg_is.i2s_config.use_apll = 0;     // not supported on ESP32-S3-BOX
    cfg_is.i2s_port = CODEC_ADC_I2S_PORT;
    cfg_is.out_rb_size = 32 * 1024;      // default is 8 * 1024
    cfg_is.type = AUDIO_STREAM_READER;
    hdl_ae_is = i2s_stream_init(&cfg_is);

    i2s_stream_set_clk(hdl_ae_is, 16000, 32, 2);

    raw_stream_cfg_t cfg_rs = RAW_STREAM_CFG_DEFAULT();
    cfg_rs.type = AUDIO_STREAM_READER;
    hdl_ae_rs_from_i2s = raw_stream_init(&cfg_rs);

    audio_pipeline_register(hdl_ap, hdl_ae_is, "i2s_stream_reader");

    audio_pipeline_register(hdl_ap, hdl_ae_rs_from_i2s, "raw_stream_reader");

    const char *tag_link[2] = {"i2s_stream_reader", "raw_stream_reader"};
    audio_pipeline_link(hdl_ap, &tag_link[0], 2);

    audio_pipeline_run(hdl_ap);

    recorder_sr_cfg_t cfg_srr = DEFAULT_RECORDER_SR_CFG();
    // E (5727) AFE_SR: sample_rate only support 16000, please modify it!
    // cfg_srr.afe_cfg.pcm_config.sample_rate = CFG_AUDIO_SR_SAMPLE_RATE;
    cfg_srr.multinet_init = false;
    cfg_srr.rb_size = 32 * 1024;

    audio_rec_cfg_t cfg_ar = AUDIO_RECORDER_DEFAULT_CFG();
    cfg_ar.read = (recorder_data_read_t)&feed_afe;
    cfg_ar.sr_handle = recorder_sr_create(&cfg_srr, &cfg_ar.sr_iface);
    cfg_ar.vad_off = 500;
    cfg_ar.event_cb = cb_ar_event;

    hdl_ar = audio_recorder_create(&cfg_ar);
}

static void at_read(void *data)
{
    const int len = 2 * 1024;
    char *buf = audio_calloc(1, len);
    int msg = -1, ret = 0;
    TickType_t delay = portMAX_DELAY;

    while (true) {
        if (xQueueReceive(q_rec, &msg, delay) == pdTRUE) {
            switch(msg) {
                case MSG_START:
                    delay = 0;
                    audio_pipeline_stop(hdl_ap_to_api);
                    audio_pipeline_wait_for_stop(hdl_ap_to_api);
                    audio_pipeline_reset_ringbuffer(hdl_ap_to_api);
                    audio_pipeline_reset_elements(hdl_ap_to_api);
                    audio_pipeline_terminate(hdl_ap_to_api);
                    audio_pipeline_run(hdl_ap_to_api);
                    stream_to_api = true;
                    // this confirms that the URI is still set correctly
                    printf("audio_pipeline_run(hdl_ap_to_api) - uri: '%s'\n", audio_element_get_uri(hdl_ae_hs));
                    break;
                case MSG_STOP:
                    delay = portMAX_DELAY;
                    audio_element_set_ringbuf_done(hdl_ae_rs_to_api);
                    stream_to_api = false;
                    break;
                default:
                    printf("at_read(): invalid msg\n");
                    break;
            }
        }

        if (stream_to_api) {
            //printf("at_read() audio_recorder_data_read()\n");
            ret = audio_recorder_data_read(hdl_ar, buf, len, portMAX_DELAY);
            if (ret <= 0) {
                printf("at_read() ret leq 0\n");
                delay = portMAX_DELAY;
                stream_to_api = false;
            }
            // calling raw_stream_read twice on the same audio element "cuts" the audio in half
            // we end up sending 1 fragment to AFE and another to the API
            // ret = raw_stream_read(hdl_ae_rs_from_i2s, (char *)buf, len);
            // if (ret <= 0) {
            //     printf("at_read() ret <= 0\n");
            //     delay = portMAX_DELAY;
            //     return;
            // }
            //printf("at_read() raw_stream_write()\n");
            raw_stream_write(hdl_ae_rs_to_api, buf, ret);
        }
    }

    free(buf);
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_VERBOSE);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    esp_err_t ret;

    esp_periph_config_t pcfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    hdl_pset = esp_periph_set_init(&pcfg);

    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    periph_wifi_cfg_t cfg_pwifi = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t hdl_pwifi = periph_wifi_init(&cfg_pwifi);

    // Start wifi
    esp_periph_start(hdl_pset, hdl_pwifi);
    periph_wifi_wait_for_connected(hdl_pwifi, portMAX_DELAY);

    audio_board_handle_t hdl_audio_board = audio_board_init();
    gpio_set_level(get_pa_enable_gpio(), 0);
    ret = audio_hal_ctrl_codec(hdl_audio_board->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    ESP_LOGI(TAG, "audio_hal_ctrl_codec: %s", esp_err_to_name(ret));

    audio_hal_set_volume(hdl_audio_board->audio_hal, 60);

    init_ap_to_api();
    start_rec();

    ESP_LOGI(TAG, "app_main() - start_rec() finished");

    q_rec = xQueueCreate(3, sizeof(int));
    audio_thread_create(NULL, "at_read", at_read, NULL, 4 * 1024, 5, true, 0);
}
