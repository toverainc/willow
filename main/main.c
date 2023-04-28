#include "audio_hal.h"
#include "audio_mem.h"
#include "audio_pipeline.h"
#include "audio_recorder.h"
#include "audio_thread.h"
#include "board.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_lcd_panel_ops.h"
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
#include "sntp.h"

#include "shared.h"

#define I2S_PORT I2S_NUM_0

static bool stream_to_api = false;
static const char *TAG = "SALLOW";
static enum q_msg {
    MSG_STOP,
    MSG_START,
};
static int total_write = 0;

static audio_element_handle_t hdl_ae_hs, hdl_ae_rs_from_i2s, hdl_ae_rs_to_api = NULL;
static audio_pipeline_handle_t hdl_ap_to_api;
static audio_rec_handle_t hdl_ar = NULL;
static esp_lcd_panel_handle_t hdl_lcd = NULL;
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

    while ((esp_timer_get_time() - start_time) < 200000) {
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
    int msg = -1;

    switch(are) {
        case AUDIO_REC_VAD_END:
            ESP_LOGI(TAG, "AUDIO_REC_VAD_END");
            msg = MSG_STOP;
            xQueueSend(q_rec, &msg, 0);
            break;
        case AUDIO_REC_VAD_START:
            ESP_LOGI(TAG, "AUDIO_REC_VAD_START");
            msg = MSG_START;
            xQueueSend(q_rec, &msg, 0);
            break;
        case AUDIO_REC_COMMAND_DECT:
            ESP_LOGI(TAG, "AUDIO_REC_COMMAND_DECT");
            break;
        case AUDIO_REC_WAKEUP_END:
            ESP_LOGI(TAG, "AUDIO_REC_WAKEUP_END");
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
            break;
        case AUDIO_REC_WAKEUP_START:
            ESP_LOGI(TAG, "AUDIO_REC_WAKEUP_START\n");
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 1023);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
            // Disable tone until we work out timing
            //audio_thread_create(NULL, "play_tone", play_tone, NULL, 4 * 1024, 5, true, 0);
            break;
        default:
            ESP_LOGI(TAG, "cb_ar_event: unhandled event: '%d'\n", are);
            break;
    }

    return ESP_OK;
}

void cb_sntp(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP client synchronized time to %lu", tv->tv_sec);
}

static int feed_afe(int16_t *buf, int len, void *ctx, TickType_t ticks)
{
    int ret = 0;
    if (buf == NULL || hdl_ae_rs_from_i2s == NULL) {
        return -1;
    }

    return raw_stream_read(hdl_ae_rs_from_i2s, (char *)buf, len);
}

static void hass_post(char *data)
{
    char hdr_auth[192];
    char json[256];
    char url[256];
    esp_err_t ret;

    esp_http_client_config_t cfg_hc = {
        // either host and path or url should be set
        .url = "http://dummy",
    };

    esp_http_client_handle_t hdl_hc = esp_http_client_init(&cfg_hc);

    snprintf(hdr_auth, 192, "Bearer %s", CONFIG_HOMEASSISTANT_TOKEN);
    snprintf(url, 256, "%s/api/conversation/process", CONFIG_HOMEASSISTANT_URI);
    snprintf(json, sizeof(data) + 224, "{\"text\":%s,\"language\":\"en\"}", data);
    ESP_LOGI(TAG, "sending '%s' to Home Assistant API on '%s'", json, url);
    esp_http_client_set_url(hdl_hc, url);
    esp_http_client_set_method(hdl_hc, HTTP_METHOD_POST);
    esp_http_client_set_header(hdl_hc, "Authorization", hdr_auth);
    esp_http_client_set_header(hdl_hc, "Content-Type", "application/json");
    esp_http_client_set_post_field(hdl_hc, json, strlen(json));
    ret = esp_http_client_perform(hdl_hc);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST status='%d'", esp_http_client_get_status_code(hdl_hc));
    } else {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(ret));
    }
    esp_http_client_cleanup(hdl_hc);
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
            hass_post(buf);

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
    ESP_LOGD(TAG, "init_ap_to_api()");
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

static esp_err_t init_sntp()
{
    ESP_LOGI(TAG, "initializing SNTP client");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
#ifdef LWIP_DHCP_GET_NTP_SRV
    sntp_servermode_dhcp(1);
#else
    sntp_setservername(0, "pool.ntp.org");
#endif
    sntp_set_time_sync_notification_cb(cb_sntp);
    sntp_init();

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
    cfg_is.out_rb_size = 8 * 1024;      // default is 8 * 1024
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

    afe_config_t cfg_afe = {
        .aec_init = true,
        .se_init = true,
        .vad_init = true,
        .wakenet_init = true,
        .voice_communication_init = false,
        .voice_communication_agc_init = false,
        .voice_communication_agc_gain = 15,
        .vad_mode = VAD_MODE_3,
        .wakenet_model_name = NULL,
        .wakenet_mode = DET_MODE_2CH_90,
        .afe_mode = SR_MODE_LOW_COST,
        .afe_perferred_core = 1,
        .afe_perferred_priority = 5,
        .afe_ringbuf_size = 50,
        .memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM,
        .agc_mode = AFE_MN_PEAK_AGC_MODE_2,
        .pcm_config.total_ch_num = 3,
        .pcm_config.mic_num = 2,
        .pcm_config.ref_num = 1,
        .pcm_config.sample_rate = 16000,
        .debug_init = false,
        .debug_hook = {{AFE_DEBUG_HOOK_MASE_TASK_IN, NULL}, {AFE_DEBUG_HOOK_FETCH_TASK_IN, NULL}},
    };

    // E (5727) AFE_SR: sample_rate only support 16000, please modify it!
    // cfg_srr.afe_cfg.pcm_config.sample_rate = CFG_AUDIO_SR_SAMPLE_RATE;

    recorder_sr_cfg_t cfg_srr = {
        .afe_cfg          = cfg_afe,
        .input_order      = INPUT_ORDER_DEFAULT(),
        .multinet_init    = false,
        .feed_task_core   = FEED_TASK_PINNED_CORE,
        .feed_task_prio   = FEED_TASK_PRIO,
        .feed_task_stack  = FEED_TASK_STACK_SZ,
        .fetch_task_core  = FETCH_TASK_PINNED_CORE,
        .fetch_task_prio  = FETCH_TASK_PRIO,
        .fetch_task_stack = FETCH_TASK_STACK_SZ,
        .rb_size          = 8 * 1024,
        .partition_label  = "model",
        .mn_language      = ESP_MN_CHINESE,
    };

    audio_rec_cfg_t cfg_ar = {
        .pinned_core    = AUDIO_REC_DEF_TASK_CORE,
        .task_prio      = AUDIO_REC_DEF_TASK_PRIO,
        .task_size      = AUDIO_REC_DEF_TASK_SZ,
        .event_cb       = cb_ar_event,
        .user_data      = NULL,
        .read           = (recorder_data_read_t)&feed_afe,
        .sr_handle      = NULL,
        .sr_iface       = NULL,
        .wakeup_time    = AUDIO_REC_DEF_WAKEUP_TM,
        .vad_start      = AUDIO_REC_VAD_START_SPEECH_MS,
        .vad_off        = 300,
        .wakeup_end     = AUDIO_REC_DEF_WAKEEND_TM,
        .encoder_handle = NULL,
        .encoder_iface  = NULL,
    };
    cfg_ar.sr_handle = recorder_sr_create(&cfg_srr, &cfg_ar.sr_iface);
    cfg_srr.afe_cfg.wakenet_model_name = WAKENET_NAME;
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

static esp_err_t init_display(void)
{
    ESP_LOGD(TAG, "initializing display");

    const ledc_channel_config_t cfg_bl_channel = {
        .channel = LEDC_CHANNEL_1,
        .duty = 0,
        .gpio_num = GPIO_NUM_45,
        .hpoint = 0,
        .intr_type = LEDC_INTR_DISABLE,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel = 1,
    };

    const ledc_timer_config_t cfg_bl_timer = {
        .clk_cfg = LEDC_AUTO_CLK,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 5000,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = 1,
    };

    int ret = ESP_OK;

    hdl_lcd = audio_board_lcd_init(hdl_pset, NULL);
    ret = esp_lcd_panel_disp_off(hdl_lcd, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to turn of display: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = ledc_channel_config(&cfg_bl_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to config LEDC channel for display backlight: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = ledc_timer_config(&cfg_bl_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to config LEDC timer for display backlight: %s", esp_err_to_name(ret));
        return ret;
    }

    return ret;
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

    init_sntp();

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

    init_display();
    init_ap_to_api();
    start_rec();

    ESP_LOGI(TAG, "app_main() - start_rec() finished");

    q_rec = xQueueCreate(3, sizeof(int));
    audio_thread_create(NULL, "at_read", at_read, NULL, 4 * 1024, 5, true, 0);
    ESP_LOGI(TAG, "Startup complete. Waiting for wake word.");
}
