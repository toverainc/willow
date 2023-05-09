#include "audio_hal.h"
#include "audio_mem.h"
#include "audio_pipeline.h"
#include "audio_recorder.h"
#include "audio_thread.h"
#include "board.h"
#include "cJSON.h"
#include "driver/ledc.h"
#include "driver/timer.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_netif.h"
#include "esp_peripherals.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "filter_resample.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "lvgl.h"
#include "model_path.h"
#include "nvs_flash.h"
#include "periph_lcd.h"
#include "periph_wifi.h"
#include "raw_stream.h"
#include "recorder_sr.h"
#include "recorder_encoder.h"
#include "sdkconfig.h"

#ifdef CONFIG_SALLOW_USE_AMRWB
#include "amrwb_encoder.h"
#endif

#ifdef CONFIG_SALLOW_USE_WAV
#include "wav_encoder.h"
#endif

#include "shared.h"
#include "tasks.h"
#include "timer.h"

#define I2S_PORT I2S_NUM_0

// this is absolutely horrendous but lvgl_port_esp32 requires esp_lcd_panel_io_handle_t and esp-adf does not expose this
typedef struct periph_lcd {
    void                                *io_bus;
    get_lcd_io_bus                      new_panel_io;
    esp_lcd_panel_io_spi_config_t       lcd_io_cfg;
    get_lcd_panel                       new_lcd_panel;
    esp_lcd_panel_dev_config_t          lcd_dev_cfg;

    esp_lcd_panel_io_handle_t           lcd_io_handle;
    esp_lcd_panel_handle_t              lcd_panel_handle;

    perph_lcd_rest                      rest_cb;
    void                                *rest_cb_ctx;
    bool                                lcd_swap_xy;
    bool                                lcd_mirror_x;
    bool                                lcd_mirror_y;
    bool                                lcd_color_invert;
} periph_lcd_t;

static bool stream_to_api = false;
typedef enum {
    MSG_STOP,
    MSG_START,
    MSG_START_LOCAL,
} q_msg;
static int total_write = 0;

static audio_element_handle_t hdl_ae_hs, hdl_ae_rs_from_i2s, hdl_ae_rs_to_api = NULL;
static audio_pipeline_handle_t hdl_ap_to_api;
static audio_rec_handle_t hdl_ar = NULL;
static esp_lcd_panel_handle_t hdl_lcd = NULL;
static lv_disp_t *ld;
static QueueHandle_t q_rec = NULL;

const int32_t tone[] = {
    0x00007fff, 0x00007fff,
    0x00000000, 0x00000000,
    0x80008000, 0x80008000,
    0x00000000, 0x00000000,
};

static void play_tone(void)
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
}

static void play_tone_ok(void *data)
{
    play_tone();
    vTaskDelete(NULL);
}

static void play_tone_err(void *data)
{
    play_tone();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    play_tone();
    vTaskDelete(NULL);
}

static esp_err_t cb_ar_event(audio_rec_evt_t are, void *data)
{
    int msg = -1;
    int command_id = 0;

    switch(are) {
        case AUDIO_REC_VAD_END:
            ESP_LOGI(TAG, "AUDIO_REC_VAD_END");
            break;
        case AUDIO_REC_VAD_START:
            ESP_LOGI(TAG, "AUDIO_REC_VAD_START");
#ifdef CONFIG_SALLOW_USE_MULTINET
            msg = MSG_START_LOCAL;
#else
            msg = MSG_START;
#endif
            xQueueSend(q_rec, &msg, 0);
            break;
        case AUDIO_REC_COMMAND_DECT:
            ESP_LOGI(TAG, "AUDIO_REC_COMMAND_DECT");
            break;
        case AUDIO_REC_WAKEUP_END:
            ESP_LOGI(TAG, "AUDIO_REC_WAKEUP_END");
            msg = MSG_STOP;
            xQueueSend(q_rec, &msg, 0);
            break;
        case AUDIO_REC_WAKEUP_START:
            ESP_LOGI(TAG, "AUDIO_REC_WAKEUP_START\n");
            timer_pause(TIMER_GROUP_0, TIMER_0);
            timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
            lvgl_port_lock(0);
            lv_obj_add_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(lbl_ln3, LV_ALIGN_CENTER, 0, 0);
            lv_label_set_text_static(lbl_ln3, "Recording command...");
            lvgl_port_unlock();
            ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, CONFIG_SALLOW_LCD_BRIGHTNESS, 0);
            // audio_thread_create(NULL, "play_tone", play_tone, NULL, 4 * 1024, 10, true, 1);
            break;
        default:
#ifdef CONFIG_SALLOW_USE_MULTINET
            // Catch all for local commands
            command_id = are;
            char command_text[64];
            int max_len = sizeof command_text;

            switch(command_id) {
                case 10:
                    snprintf(command_text, max_len, "%s", "TURN ON THE TV");
                    break;
                case 11:
                    snprintf(command_text, max_len, "%s", "TURN OFF THE TV");
                    break;
                default:
                    snprintf(command_text, max_len, "%s", "UNKNOWN");
                    break;
            }

            ESP_LOGI(TAG, "Got local command ID: '%d'\n", command_id);
            lvgl_port_lock(0);
            lv_obj_clear_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);

            lv_label_set_text_static(lbl_ln1, "I heard command:");
            lv_label_set_text(lbl_ln2, command_text);
            lvgl_port_unlock();
            timer_start(TIMER_GROUP_0, TIMER_0);
#else
            ESP_LOGI(TAG, "cb_ar_event: unhandled event: '%d'\n", are);
#endif
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
    if (buf == NULL || hdl_ae_rs_from_i2s == NULL) {
        return -1;
    }

    return raw_stream_read(hdl_ae_rs_from_i2s, (char *)buf, len);
}

static void hass_post(char *data)
{
    bool ok;
    char *body = NULL;
    char *hdr_auth = NULL;
    char *json = NULL;
    esp_err_t ret;
    int n;

    esp_http_client_config_t cfg_hc = {
        // either host and path or url should be set
        .url = "http://dummy",
    };

    esp_http_client_handle_t hdl_hc = esp_http_client_init(&cfg_hc);

    hdr_auth = malloc(8 + strlen(CONFIG_HOMEASSISTANT_TOKEN));

    snprintf(hdr_auth, 8 + strlen(CONFIG_HOMEASSISTANT_TOKEN), "Bearer %s", CONFIG_HOMEASSISTANT_TOKEN);

    ESP_LOGI(TAG, "sending '%s' to Home Assistant API on '%s'", data, CONFIG_HOMEASSISTANT_URI);
    esp_http_client_set_url(hdl_hc, CONFIG_HOMEASSISTANT_URI);
    esp_http_client_set_method(hdl_hc, HTTP_METHOD_POST);
    esp_http_client_set_header(hdl_hc, "Authorization", hdr_auth);
    esp_http_client_set_header(hdl_hc, "Content-Type", "application/json");
    ret = esp_http_client_open(hdl_hc, strlen(data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to open HTTP connection: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    n = esp_http_client_write(hdl_hc, data, strlen(data));
    if (n < 0) {
        ESP_LOGE(TAG, "failed to POST HTTP data");
        goto cleanup;
    }
    n = esp_http_client_fetch_headers(hdl_hc);
    if (n < 0) {
        ESP_LOGE(TAG, "failed to get HTTP headers");
        goto cleanup;
    }
    body = malloc(n + 1);
    n = esp_http_client_read_response(hdl_hc, body, n);
    if (n >= 0) {
        int http_status = esp_http_client_get_status_code(hdl_hc);
        ESP_LOGI(TAG, "HTTP POST status='%d' content_length='%d'",
                 http_status, esp_http_client_get_content_length(hdl_hc));
        if (http_status != 200) {
            ok = false;
            audio_thread_create(NULL, "play_tone_err", play_tone_err, NULL, 4 * 1024, 10, true, 1);
        }
        cJSON *cjson = cJSON_Parse(body);
        cJSON *response = cJSON_GetObjectItemCaseSensitive(cjson, "response");
        if (cJSON_IsObject(response)) {
            cJSON *response_type = cJSON_GetObjectItemCaseSensitive(response, "response_type");
            if (cJSON_IsString(response_type) && response_type->valuestring != NULL) {
                ESP_LOGI(TAG, "home assistant response_type: %s", response_type->valuestring);
                if (!strcmp(response_type->valuestring, "error")) {
                    ok = false;
                    audio_thread_create(NULL, "play_tone_err", play_tone_err, NULL, 4 * 1024, 10, true, 1);
                } else {
                    ok = true;
                    audio_thread_create(NULL, "play_tone_ok", play_tone_ok, NULL, 4 * 1024, 10, true, 1);
                }
            }
        }
        json = cJSON_Print(cjson);
        cJSON_Delete(cjson);
        if (json != NULL) {
            ESP_LOGI(TAG, "HTTP POST response body:\n%s", json);
        }

        lvgl_port_lock(0);
        lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(lbl_ln3, LV_ALIGN_TOP_LEFT, 0, 120);
        lv_label_set_text_static(lbl_ln3, "Command status:");
        lv_label_set_text(lbl_ln4, ok ? "#008000 Success!" : "#ff0000 Something went wrong");
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "failed to read HTTP POST response");
    }
    timer_start(TIMER_GROUP_0, TIMER_0);
    free(body);
cleanup:
    esp_http_client_cleanup(hdl_hc);

    free(hdr_auth);
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
#ifdef CONFIG_SALLOW_USE_AMRWB
            esp_http_client_set_header(http, "x-audio-codec", "amrwb");
#endif
#ifdef CONFIG_SALLOW_USE_WAV
            esp_http_client_set_header(http, "x-audio-codec", "wav");
#endif
#ifdef CONFIG_SALLOW_USE_PCM
            esp_http_client_set_header(http, "x-audio-codec", "pcm");
#endif
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

            cJSON *cjson = cJSON_Parse(buf);
            cJSON *text = cJSON_GetObjectItemCaseSensitive(cjson, "text");

            lvgl_port_lock(0);
            lv_obj_clear_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text_static(lbl_ln1, "I heard:");
            if (cJSON_IsString(text) && text->valuestring != NULL) {
                lv_label_set_text(lbl_ln2, text->valuestring);
            } else {
                lv_label_set_text(lbl_ln2, buf);
            }
            lvgl_port_unlock();

            cJSON_Delete(cjson);

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
    cfg_rs.out_rb_size = 64 * 1024;     // default is 8 * 1024
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
    setenv("TZ", CONFIG_SALLOW_TIMEZONE, 1);
    tzset();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
#ifdef CONFIG_SALLOW_NTP_USE_DHCP
    ESP_LOGI(TAG, "Using DHCP SNTP server");
    sntp_servermode_dhcp(1);
#else
    ESP_LOGI(TAG, "Using configured SNTP server '%s'", CONFIG_SALLOW_NTP_HOST);
    sntp_setservername(0, CONFIG_SALLOW_NTP_HOST);
#endif
    sntp_set_time_sync_notification_cb(cb_sntp);
    sntp_init();

    return ESP_OK;
}

static void start_rec()
{
    audio_element_handle_t hdl_ae_is;
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
        .afe_mode = SR_MODE_HIGH_PERF,
        .afe_perferred_core = 1,
        .afe_perferred_priority = 5,
        .afe_ringbuf_size = 50,
        .memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM,
        .agc_mode = AFE_MN_PEAK_AGC_MODE_3,
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
        .rb_size          = 12 * 1024,   // default is 6 * 1024
        .partition_label  = "model",
        .mn_language      = ESP_MN_ENGLISH,
    };

#ifdef CONFIG_SALLOW_USE_MULTINET
    ESP_LOGI(TAG, "Using local multinet");
    cfg_srr.multinet_init = true;
#endif

#ifdef CONFIG_SALLOW_RECORD_BUFFER
    ESP_LOGI(TAG, "Using record buffer '%d'", CONFIG_SALLOW_RECORD_BUFFER);
    cfg_srr.rb_size = CONFIG_SALLOW_RECORD_BUFFER * 1024;
#endif

#ifdef CONFIG_SALLOW_USE_AMRWB
    recorder_encoder_cfg_t recorder_encoder_cfg = { 0 };
    amrwb_encoder_cfg_t amrwb_cfg = DEFAULT_AMRWB_ENCODER_CONFIG();
    amrwb_cfg.contain_amrwb_header = true;
    amrwb_cfg.stack_in_ext = true;
    amrwb_cfg.task_core = 0;
    amrwb_cfg.task_prio = 5;
    amrwb_cfg.out_rb_size = 8 * 1024;
    amrwb_cfg.bitrate_mode = AMRWB_ENC_BITRATE_MD2385;

    recorder_encoder_cfg.encoder = amrwb_encoder_init(&amrwb_cfg);
#endif

#ifdef CONFIG_SALLOW_USE_WAV
    recorder_encoder_cfg_t recorder_encoder_cfg = { 0 };
    wav_encoder_cfg_t wav_cfg = DEFAULT_WAV_ENCODER_CONFIG();
    wav_cfg.stack_in_ext = true;
    wav_cfg.task_core = 0;
    wav_cfg.task_prio = 5;
    wav_cfg.out_rb_size = 8 * 1024;

    recorder_encoder_cfg.encoder = wav_encoder_init(&wav_cfg);
#endif

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
        .wakeup_end     = 100,
        .encoder_handle = NULL,
        .encoder_iface  = NULL,
    };
    cfg_ar.sr_handle = recorder_sr_create(&cfg_srr, &cfg_ar.sr_iface);
#if defined(CONFIG_SALLOW_USE_AMRWB) || defined(CONFIG_SALLOW_USE_WAV)
    ESP_LOGI(TAG, "Using recorder encoder");
    cfg_ar.encoder_handle = recorder_encoder_create(&recorder_encoder_cfg, &cfg_ar.encoder_iface);
#endif
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
                    printf("at_read(): invalid msg '%d'\n", msg);
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
        .duty = CONFIG_SALLOW_LCD_BRIGHTNESS,
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

    ret = ledc_timer_config(&cfg_bl_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to config LEDC timer for display backlight: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = ledc_channel_config(&cfg_bl_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to config LEDC channel for display backlight: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = ledc_fade_func_install(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to install LEDC fade function: %s", esp_err_to_name(ret));
        return ret;
    }

    return ret;
}

static esp_err_t init_lvgl(void)
{
    esp_err_t ret = ESP_OK;
    const lvgl_port_cfg_t cfg_lp = ESP_LVGL_PORT_INIT_CONFIG();
    ret = lvgl_port_init(&cfg_lp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize LVGL port: %s", esp_err_to_name(ret));
        return ret;
    }

    // get peripheral handle for LCD
    esp_periph_handle_t hdl_plcd = esp_periph_set_get_by_id(hdl_pset, PERIPH_ID_LCD);

    // get data for LCD peripheral
    periph_lcd_t *lcdp = esp_periph_get_data(hdl_plcd);

    if (lcdp == NULL || lcdp->lcd_io_handle == NULL) {
        ESP_LOGE(TAG, "failed to get LCD IO handle");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "init_lvgl: lcdp->lcd_io_handle: %p", lcdp->lcd_io_handle);

    const lvgl_port_display_cfg_t cfg_ld = {
        .buffer_size = LCD_H_RES * LCD_V_RES / 10,
        .double_buffer = false,
        // DMA and SPIRAM
        // E (16:37:21.267) LVGL: lvgl_port_add_disp(190): Alloc DMA capable buffer in SPIRAM is not supported!
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
        },
        .hres = LCD_H_RES,
        // confirmed this is correct by printf %p periph_lcd->lcd_io_handle in esp_peripherals/periph_lcd.c
        .io_handle = lcdp->lcd_io_handle,
        .monochrome = false,
        .panel_handle = hdl_lcd,
        .rotation = {
            .mirror_x = LCD_MIRROR_X,
            .mirror_y = LCD_MIRROR_Y,
            .swap_xy = LCD_SWAP_XY,
        },
        .vres = LCD_V_RES,
    };

    ld = lvgl_port_add_disp(&cfg_ld);

    esp_lcd_touch_config_t cfg_lt = {
        .flags = {
            .mirror_x = LCD_MIRROR_X,
            .mirror_y = LCD_MIRROR_Y,
            .swap_xy = LCD_SWAP_XY,
        },
        .levels = {
            .interrupt = 0,
            .reset = 0,
        },
        .int_gpio_num = -1,
        .rst_gpio_num = -1,
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
    };
    esp_lcd_touch_handle_t hdl_lt;

    ret =  esp_lcd_touch_new_i2c_gt911(lcdp->lcd_io_handle, &cfg_lt, &hdl_lt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize touch screen: %s", esp_err_to_name(ret));
        return ret;
    }

    const lvgl_port_touch_cfg_t cfg_pt = {
        .disp = ld,
        .handle = hdl_lt,
    };

    lv_indev_t *lt = lvgl_port_add_touch(&cfg_pt);
    lv_indev_enable(lt, true);

    LV_IMG_DECLARE(lv_img_hand_left);
    lv_obj_t *oc = lv_img_create(lv_scr_act());
    lv_img_set_src(oc, &lv_img_hand_left);
    lv_indev_set_cursor(lt, oc);

    return ret;
}

static void cb_scr(lv_event_t *ev)
{
    // printf("cb_scr\n");
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_VERBOSE);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    esp_err_t ret;

    esp_periph_config_t pcfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    pcfg.extern_stack = true;
    hdl_pset = esp_periph_set_init(&pcfg);

    init_display();
    init_lvgl();

    if (ld == NULL) {
        ESP_LOGE(TAG, "lv_disp_t ld is NULL!!!!");
    } else {
        // static lv_style_t lv_st_montserrat_20;
        // lv_style_init(&lv_st_montserrat_20);
        // lv_style_set_text_color(&lv_st_montserrat_20, lv_color_black());
        // lv_style_set_text_font(&lv_st_montserrat_20, &lv_font_montserrat_14);
        // lv_style_set_text_opa(&lv_st_montserrat_20, LV_OPA_30);

        lvgl_port_lock(0);

        lv_obj_t *scr_act = lv_disp_get_scr_act(ld);
        lv_obj_t *lbl_hdr = lv_label_create(scr_act);
        lbl_ln1 = lv_label_create(scr_act);
        lbl_ln2 = lv_label_create(scr_act);
        lbl_ln3 = lv_label_create(scr_act);
        lbl_ln4 = lv_label_create(scr_act);
        lv_label_set_recolor(lbl_ln4, true);
        lv_obj_add_event_cb(scr_act, cb_scr, LV_EVENT_ALL, NULL);
        // lv_obj_add_style(lbl_hdr, &lv_st_montserrat_20, 0);
        lv_label_set_text_static(lbl_hdr, "Welcome to Sallow!");
        lv_obj_add_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(lbl_hdr, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_align(lbl_ln1, LV_ALIGN_TOP_LEFT, 0, 30);
        lv_obj_align(lbl_ln2, LV_ALIGN_TOP_LEFT, 0, 60);
        lv_obj_align(lbl_ln3, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(lbl_ln4, LV_ALIGN_TOP_LEFT, 0, 150);
        lv_label_set_long_mode(lbl_ln2, LV_LABEL_LONG_SCROLL);
        lv_obj_set_width(lbl_ln2, 320);
        lv_label_set_text_static(lbl_ln3, "Starting up...");

        lvgl_port_unlock();
    }

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

    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set Wi-Fi power save mode");
    }

    audio_board_handle_t hdl_audio_board = audio_board_init();
    gpio_set_level(get_pa_enable_gpio(), 0);
    ret = audio_hal_ctrl_codec(hdl_audio_board->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    ESP_LOGI(TAG, "audio_hal_ctrl_codec: %s", esp_err_to_name(ret));

    audio_hal_set_volume(hdl_audio_board->audio_hal, CONFIG_SALLOW_VOLUME);

    init_timer();
    init_ap_to_api();
    start_rec();

    ESP_LOGI(TAG, "app_main() - start_rec() finished");

    q_rec = xQueueCreate(3, sizeof(int));
    audio_thread_create(NULL, "at_read", at_read, NULL, 4 * 1024, 5, true, 0);

    ESP_LOGI(TAG, "esp_netif_get_nr_of_ifs: %d", esp_netif_get_nr_of_ifs());
    esp_netif_t *hdl_netif = esp_netif_next(NULL);

    if (hdl_netif != NULL) {
        char wake_help[32] = "Ready!";

#ifdef CONFIG_SR_WN_WN9_HIESP
        snprintf(wake_help, 32, "Say 'Hi ESP' to start!");
#endif

#ifdef CONFIG_SR_WN_WN9_ALEXA
        snprintf(wake_help, 32, "Say 'Alexa' to start!");
#endif

        if (ld == NULL) {
            ESP_LOGE(TAG, "lv_disp_t ld is NULL!!!!");
        } else {
            // static lv_style_t lv_st_montserrat_20;
            // lv_style_init(&lv_st_montserrat_20);
            // lv_style_set_text_color(&lv_st_montserrat_20, lv_color_black());
            // lv_style_set_text_font(&lv_st_montserrat_20, &lv_font_montserrat_14);
            // lv_style_set_text_opa(&lv_st_montserrat_20, LV_OPA_30);

            lvgl_port_lock(0);
            lv_label_set_text_static(lbl_ln3, wake_help);
            lvgl_port_unlock();
        }
    }

    ESP_LOGI(TAG, "Startup complete. Waiting for wake word.");

    ESP_ERROR_CHECK_WITHOUT_ABORT(timer_start(TIMER_GROUP_0, TIMER_0));

    while (true) {
#ifdef CONFIG_SALLOW_DEBUG_MEM
        printf("MALLOC_CAP_INTERNAL:\n");
        heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
        printf("MALLOC_CAP_SPIRAM:\n");
        heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
#endif
#ifdef CONFIG_SALLOW_DEBUG_TASKS
        char buf[128];
        vTaskList(&buf);
        printf("%s\n", buf);
#endif
	    vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
