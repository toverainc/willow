#include "audio_hal.h"
#include "audio_mem.h"
#include "audio_pipeline.h"
#include "audio_recorder.h"
#include "audio_thread.h"
#include "board.h"
#include "cJSON.h"
#include "driver/ledc.h"
#include "driver/timer.h"
#include "es7210.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_netif.h"
#include "esp_peripherals.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "filter_resample.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "input_key_service.h"
#include "lvgl.h"
#include "model_path.h"
#include "nvs_flash.h"
#include "periph_button.h"
#include "periph_wifi.h"
#include "raw_stream.h"
#include "recorder_encoder.h"
#include "recorder_sr.h"
#include "sdkconfig.h"

#ifdef CONFIG_WILLOW_USE_AMRWB
#include "amrwb_encoder.h"
#endif

#ifdef CONFIG_WILLOW_USE_WAV
#include "wav_encoder.h"
#endif

#ifdef CONFIG_WILLOW_USE_MULTINET
#include "generated_cmd_multinet.h"
#endif

#include "shared.h"
#include "slvgl.h"
#include "tasks.h"
#include "timer.h"

#if defined(CONFIG_WILLOW_USE_ENDPOINT_HOMEASSISTANT)
#include "endpoint/hass.h"
#elif defined(CONFIG_WILLOW_USE_ENDPOINT_OPENHAB)
#include "endpoint/openhab.h"
#elif defined(CONFIG_WILLOW_USE_ENDPOINT_REST)
#include "endpoint/rest.h"
#endif

#define I2S_PORT I2S_NUM_0

bool recording = false;

static bool stream_to_api = false;
static int total_write = 0;

static audio_element_handle_t hdl_ae_hs, hdl_ae_rs_from_i2s, hdl_ae_rs_to_api = NULL;
static audio_pipeline_handle_t hdl_ap_to_api;
static audio_rec_handle_t hdl_ar = NULL;

QueueHandle_t q_rec;
esp_lcd_panel_handle_t hdl_lcd;

// clang-format off
const int32_t tone[] = {
    0x00007fff, 0x00007fff,
    0x00000000, 0x00000000,
    0x80008000, 0x80008000,
    0x00000000, 0x00000000,
};
// clang-format on

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

void play_tone_ok(void *data)
{
    play_tone();
    vTaskDelete(NULL);
}

void play_tone_err(void *data)
{
    play_tone();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    play_tone();
    vTaskDelete(NULL);
}

static esp_err_t cb_ar_event(audio_rec_evt_t are, void *data)
{
    int msg = -1;
#ifdef CONFIG_WILLOW_USE_MULTINET
    int command_id = 0;
#endif

    switch (are) {
        case AUDIO_REC_VAD_END:
            ESP_LOGI(TAG, "AUDIO_REC_VAD_END");
            if (esp_timer_is_active(hdl_sess_timer)) {
                esp_timer_stop(hdl_sess_timer);
            }
            break;
        case AUDIO_REC_VAD_START:
            ESP_LOGI(TAG, "AUDIO_REC_VAD_START");
#ifdef CONFIG_WILLOW_USE_MULTINET
            msg = MSG_START_LOCAL;
#else
            msg = MSG_START;
#endif
            xQueueSend(q_rec, &msg, 0);
            if (esp_timer_is_active(hdl_sess_timer)) {
                esp_timer_stop(hdl_sess_timer);
            }
            esp_timer_start_once(hdl_sess_timer, CONFIG_WILLOW_STREAM_TIMEOUT * 1000 * 1000);
            break;
        case AUDIO_REC_COMMAND_DECT:
            // Multinet timeout
            ESP_LOGI(TAG, "AUDIO_REC_COMMAND_DECT");
            audio_thread_create(NULL, "play_tone_err", play_tone_err, NULL, 4 * 1024, 10, true, 1);
            lvgl_port_lock(0);
            lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);

            lv_label_set_text(lbl_ln3, "#ff0000 Unrecognized Command");
            lvgl_port_unlock();
            timer_start(TIMER_GROUP_0, TIMER_0);
            break;
        case AUDIO_REC_WAKEUP_END:
            ESP_LOGI(TAG, "AUDIO_REC_WAKEUP_END");
            msg = MSG_STOP;
            xQueueSend(q_rec, &msg, 0);
            break;
        case AUDIO_REC_WAKEUP_START:
            ESP_LOGI(TAG, "AUDIO_REC_WAKEUP_START\n");
            if (recording) {
                msg = MSG_STOP;
                xQueueSend(q_rec, &msg, 0);
                break;
            }
            reset_timer(true);
            lvgl_port_lock(0);
            lv_obj_add_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(btn_cancel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(lbl_ln3, LV_ALIGN_CENTER, 0, 0);
#ifdef CONFIG_WILLOW_USE_MULTINET
            lv_label_set_text_static(lbl_ln3, "Say local command...");
#else
            lv_label_set_text_static(lbl_ln3, "Say command...");
#endif
            lv_obj_add_event_cb(btn_cancel, cb_btn_cancel, LV_EVENT_PRESSED, NULL);
            lvgl_port_unlock();
            ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, CONFIG_WILLOW_LCD_BRIGHTNESS, 0);
            // audio_thread_create(NULL, "play_tone", play_tone, NULL, 4 * 1024, 10, true, 1);
            break;
        default:
#ifdef CONFIG_WILLOW_USE_MULTINET
            // Catch all for local commands
            command_id = are;
            char *json;
            json = calloc(sizeof(char), 29 + strlen(lookup_cmd_multinet(command_id)));
            snprintf(json, 29 + strlen(lookup_cmd_multinet(command_id)), "{\"text\":\"%s\",\"language\":\"en\"}",
                     lookup_cmd_multinet(command_id));
#if defined(CONFIG_WILLOW_USE_ENDPOINT_HOMEASSISTANT)
            hass_send(json);
#elif defined(CONFIG_WILLOW_USE_ENDPOINT_OPENHAB)
            openhab_send(lookup_cmd_multinet(command_id));
#elif defined(CONFIG_WILLOW_USE_ENDPOINT_REST)
            rest_send(json);
#endif
            free(json);

            ESP_LOGI(TAG, "Got local command ID: '%d'\n", command_id);
            lvgl_port_lock(0);
            lv_obj_clear_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);

            lv_label_set_text_static(lbl_ln1, "I heard command:");
            lv_label_set_text(lbl_ln2, lookup_cmd_multinet(command_id));
            lvgl_port_unlock();
            timer_start(TIMER_GROUP_0, TIMER_0);
#else
            ESP_LOGI(TAG, "cb_ar_event: unhandled event: '%d'\n", are);
#endif
            break;
    }

    return ESP_OK;
}

static esp_err_t cb_iks(periph_service_handle_t hdl, periph_service_event_t *ev, void *data)
{
    int key = (int)ev->data;
    int ret = ESP_OK;
    ESP_LOGD(TAG, "key pressed: type='%d', key='%d'", ev->type, key);

    if (key == INPUT_KEY_USER_ID_MUTE) {
        if (ev->type == INPUT_KEY_SERVICE_ACTION_PRESS_RELEASE) {
            ESP_LOGI(TAG, "unmute");
            audio_hal_codec_config_t cfg_ahc = {
                .adc_input  = AUDIO_HAL_ADC_INPUT_LINE1,
                .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,
                .codec_mode = AUDIO_HAL_CODEC_MODE_BOTH,
                .i2s_iface = {
                    .mode = AUDIO_HAL_MODE_SLAVE,
                    .fmt = AUDIO_HAL_I2S_NORMAL,
                    .samples = AUDIO_HAL_16K_SAMPLES,
                    .bits = AUDIO_HAL_BIT_LENGTH_32BITS,
                },
            };

            es7210_adc_init(&cfg_ahc);
            es7210_adc_set_gain(GAIN_37_5DB);
        }
    }

    return ret;
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

esp_err_t hdl_ev_hs(http_stream_event_msg_t *msg)
{
    if (msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_handle_t http = (esp_http_client_handle_t)msg->http_client;
    char len_buf[16];
    int wlen = 0;

    switch (msg->event_id) {
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
#ifdef CONFIG_WILLOW_USE_AMRWB
            esp_http_client_set_header(http, "x-audio-codec", "amrwb");
#endif
#ifdef CONFIG_WILLOW_USE_WAV
            esp_http_client_set_header(http, "x-audio-codec", "wav");
#endif
#ifdef CONFIG_WILLOW_USE_PCM
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
            char *buf = calloc(sizeof(char), 2048);
            assert(buf);
            int read_len = esp_http_client_read(http, buf, 2048);
            if (read_len <= 0) {
                free(buf);
                return ESP_FAIL;
            }
            buf[read_len] = 0;
            ESP_LOGI(TAG, "Got HTTP Response = %s", (char *)buf);
#if defined(CONFIG_WILLOW_USE_ENDPOINT_HOMEASSISTANT)
            hass_send(buf);
#elif defined(CONFIG_WILLOW_USE_ENDPOINT_OPENHAB)
            openhab_send(buf);
#elif defined(CONFIG_WILLOW_USE_ENDPOINT_REST)
            rest_send(buf);
#endif

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
    // audio_element_handle_t hdl_ae_hs;
    audio_pipeline_cfg_t cfg_ap = DEFAULT_AUDIO_PIPELINE_CONFIG();
    hdl_ap_to_api = audio_pipeline_init(&cfg_ap);

    http_stream_cfg_t cfg_hs = HTTP_STREAM_CFG_DEFAULT();
    cfg_hs.event_handle = hdl_ev_hs;
    cfg_hs.type = AUDIO_STREAM_WRITER;
    hdl_ae_hs = http_stream_init(&cfg_hs);

    raw_stream_cfg_t cfg_rs = RAW_STREAM_CFG_DEFAULT();
    cfg_rs.out_rb_size = 64 * 1024; // default is 8 * 1024
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
    setenv("TZ", CONFIG_WILLOW_TIMEZONE, 1);
    tzset();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
#ifdef CONFIG_WILLOW_NTP_USE_DHCP
    ESP_LOGI(TAG, "Using DHCP SNTP server");
    sntp_servermode_dhcp(1);
#else
    ESP_LOGI(TAG, "Using configured SNTP server '%s'", CONFIG_WILLOW_NTP_HOST);
    sntp_setservername(0, CONFIG_WILLOW_NTP_HOST);
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
    cfg_is.i2s_config.use_apll = 0; // not supported on ESP32-S3-BOX
    cfg_is.i2s_port = CODEC_ADC_I2S_PORT;
    cfg_is.out_rb_size = 8 * 1024; // default is 8 * 1024
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
        .afe_cfg = cfg_afe,
        .input_order = INPUT_ORDER_DEFAULT(),
        .multinet_init = false,
        .feed_task_core = FEED_TASK_PINNED_CORE,
        .feed_task_prio = FEED_TASK_PRIO,
        .feed_task_stack = FEED_TASK_STACK_SZ,
        .fetch_task_core = FETCH_TASK_PINNED_CORE,
        .fetch_task_prio = FETCH_TASK_PRIO,
        .fetch_task_stack = FETCH_TASK_STACK_SZ,
        .rb_size = 12 * 1024, // default is 6 * 1024
        .partition_label = "model",
        .mn_language = ESP_MN_ENGLISH,
    };

#ifdef CONFIG_WILLOW_RECORD_BUFFER
    ESP_LOGI(TAG, "Using record buffer '%d'", CONFIG_WILLOW_RECORD_BUFFER);
    cfg_srr.rb_size = CONFIG_WILLOW_RECORD_BUFFER * 1024;
#endif

#ifdef CONFIG_WILLOW_USE_MULTINET
    ESP_LOGI(TAG, "Using local multinet");
    cfg_srr.multinet_init = true;
    cfg_srr.rb_size = 6 * 1024;
#endif

#ifdef CONFIG_WILLOW_USE_AMRWB
    recorder_encoder_cfg_t recorder_encoder_cfg = {0};
    amrwb_encoder_cfg_t amrwb_cfg = DEFAULT_AMRWB_ENCODER_CONFIG();
    amrwb_cfg.contain_amrwb_header = true;
    amrwb_cfg.stack_in_ext = true;
    amrwb_cfg.task_core = 0;
    amrwb_cfg.task_prio = 5;
    amrwb_cfg.out_rb_size = 8 * 1024;
    amrwb_cfg.bitrate_mode = AMRWB_ENC_BITRATE_MD2385;

    recorder_encoder_cfg.encoder = amrwb_encoder_init(&amrwb_cfg);
#endif

#ifdef CONFIG_WILLOW_USE_WAV
    recorder_encoder_cfg_t recorder_encoder_cfg = {0};
    wav_encoder_cfg_t wav_cfg = DEFAULT_WAV_ENCODER_CONFIG();
    wav_cfg.stack_in_ext = true;
    wav_cfg.task_core = 0;
    wav_cfg.task_prio = 5;
    wav_cfg.out_rb_size = 8 * 1024;

    recorder_encoder_cfg.encoder = wav_encoder_init(&wav_cfg);
#endif

    audio_rec_cfg_t cfg_ar = {
        .pinned_core = AUDIO_REC_DEF_TASK_CORE,
        .task_prio = AUDIO_REC_DEF_TASK_PRIO,
        .task_size = AUDIO_REC_DEF_TASK_SZ,
        .event_cb = cb_ar_event,
        .user_data = NULL,
        .read = (recorder_data_read_t)&feed_afe,
        .sr_handle = NULL,
        .sr_iface = NULL,
        .wakeup_time = AUDIO_REC_DEF_WAKEUP_TM,
        .vad_start = AUDIO_REC_VAD_START_SPEECH_MS,
        .vad_off = CONFIG_WILLOW_VAD_TIMEOUT,
        .wakeup_end = 1,
        .encoder_handle = NULL,
        .encoder_iface = NULL,
    };
    cfg_ar.sr_handle = recorder_sr_create(&cfg_srr, &cfg_ar.sr_iface);
#if defined(CONFIG_WILLOW_USE_AMRWB) || defined(CONFIG_WILLOW_USE_WAV)
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
            switch (msg) {
                case MSG_START:
                    delay = 0;
                    recording = true;
                    audio_pipeline_stop(hdl_ap_to_api);
                    audio_pipeline_wait_for_stop(hdl_ap_to_api);
                    audio_pipeline_reset_ringbuffer(hdl_ap_to_api);
                    audio_pipeline_reset_elements(hdl_ap_to_api);
                    audio_pipeline_terminate(hdl_ap_to_api);
                    audio_pipeline_run(hdl_ap_to_api);
                    stream_to_api = true;
                    // this confirms that the URI is still set correctly
                    printf("audio_pipeline_run(hdl_ap_to_api) - uri: '%s'\n", audio_element_get_uri(hdl_ae_hs));
                    __attribute__((fallthrough));
                case MSG_START_LOCAL:
                    recording = true;
                    break;
                case MSG_STOP:
                    delay = portMAX_DELAY;
                    audio_element_set_ringbuf_done(hdl_ae_rs_to_api);
                    recording = false;
                    stream_to_api = false;
                    lv_obj_add_flag(btn_cancel, LV_OBJ_FLAG_HIDDEN);
                    break;
                default:
                    printf("at_read(): invalid msg '%d'\n", msg);
                    break;
            }
        }

        if (stream_to_api) {
            // printf("at_read() audio_recorder_data_read()\n");
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
            // printf("at_read() raw_stream_write()\n");
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
        .duty = CONFIG_WILLOW_LCD_BRIGHTNESS,
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

static esp_err_t init_buttons(void)
{
    periph_button_cfg_t cfg_btn = {
        .gpio_mask = GPIO_SEL_0 | GPIO_SEL_1, // BOOT/CONFIG | MUTE
    };
    esp_periph_handle_t hdl_btn = periph_button_init(&cfg_btn);
    if (hdl_btn == NULL) {
        return ESP_ERR_ADF_MEMORY_LACK;
    }
    return esp_periph_start(hdl_pset, hdl_btn);
}

static esp_err_t init_input_key_service()
{
    int ret = ESP_OK;
    input_key_service_info_t inf_iks_boot = {
        .act_id = GPIO_NUM_0,
        .type = PERIPH_ID_BUTTON,
        .user_id = INPUT_KEY_USER_ID_REC,
    };
    input_key_service_info_t inf_iks_mute = {
        .act_id = GPIO_NUM_1,
        .type = PERIPH_ID_BUTTON,
        .user_id = INPUT_KEY_USER_ID_MUTE,
    };
    input_key_service_info_t inf_iks[] = {
        inf_iks_boot,
        inf_iks_mute,
    };

    input_key_service_cfg_t cfg_iks = INPUT_KEY_SERVICE_DEFAULT_CONFIG();
    cfg_iks.handle = hdl_pset;
    periph_service_handle_t hld_psvc_iks = input_key_service_create(&cfg_iks);
    ret = input_key_service_add_key(hld_psvc_iks, inf_iks, INPUT_KEY_NUM);
    if (ret != ESP_OK) {
        return ret;
    }
    return periph_service_set_callback(hld_psvc_iks, cb_iks, NULL);
}

#define MAC_ADDR_SIZE 6
uint8_t mac_address[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static void get_mac_address()
{
    uint8_t mac[MAC_ADDR_SIZE];
    esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    ESP_LOGI(TAG, "MAC address: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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
    init_lvgl_display();

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
        btn_cancel = lv_btn_create(scr_act);
        lbl_btn_cancel = lv_label_create(btn_cancel);
        lbl_ln1 = lv_label_create(scr_act);
        lbl_ln2 = lv_label_create(scr_act);
        lbl_ln3 = lv_label_create(scr_act);
        lbl_ln4 = lv_label_create(scr_act);
        lv_label_set_recolor(lbl_ln3, true);
        lv_label_set_recolor(lbl_ln4, true);
        lv_obj_add_event_cb(scr_act, cb_scr, LV_EVENT_ALL, NULL);
        // lv_obj_add_style(lbl_hdr, &lv_st_montserrat_20, 0);
        lv_label_set_text_static(lbl_btn_cancel, "Cancel");
        lv_label_set_text_static(lbl_hdr, "Welcome to Willow!");
        lv_obj_add_flag(btn_cancel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_align(lbl_btn_cancel, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(lbl_hdr, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_align(lbl_ln1, LV_ALIGN_TOP_LEFT, 0, 30);
        lv_obj_align(lbl_ln2, LV_ALIGN_TOP_LEFT, 0, 60);
        lv_obj_align(lbl_ln3, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(lbl_ln4, LV_ALIGN_TOP_LEFT, 0, 150);
        lv_label_set_long_mode(lbl_ln2, LV_LABEL_LONG_SCROLL);
        lv_label_set_long_mode(lbl_ln4, LV_LABEL_LONG_SCROLL);
        lv_obj_set_width(lbl_ln2, 320);
        lv_obj_set_width(lbl_ln4, 320);
#ifdef CONFIG_WILLOW_USE_MULTINET
        lv_label_set_text_static(lbl_ln3, "Starting up (local)...");
#else
        lv_label_set_text_static(lbl_ln3, "Starting up (server)...");
#endif
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

    init_sntp();

    audio_board_handle_t hdl_audio_board = audio_board_init();
    gpio_set_level(get_pa_enable_gpio(), 0);
    ret = audio_hal_ctrl_codec(hdl_audio_board->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    ESP_LOGI(TAG, "audio_hal_ctrl_codec: %s", esp_err_to_name(ret));

    audio_hal_set_volume(hdl_audio_board->audio_hal, CONFIG_WILLOW_VOLUME);

#ifdef CONFIG_WILLOW_USE_ENDPOINT_HOMEASSISTANT
    init_hass();
#endif
    init_buttons();
    init_input_key_service();
    init_lvgl_touch();
    init_session_timer();
    init_timer();
    init_ap_to_api();
    start_rec();

    ESP_LOGI(TAG, "app_main() - start_rec() finished");

    q_rec = xQueueCreate(3, sizeof(int));
    audio_thread_create(NULL, "at_read", at_read, NULL, 4 * 1024, 5, true, 0);

    ESP_LOGI(TAG, "esp_netif_get_nr_of_ifs: %d", esp_netif_get_nr_of_ifs());
    esp_netif_t *hdl_netif = esp_netif_next(NULL);

    if (hdl_netif != NULL) {
#if defined(CONFIG_SR_WN_WN9_HIESP)
        char *wake_help = "Say 'Hi ESP' to start!";
#elif defined(CONFIG_SR_WN_WN9_ALEXA)
        char *wake_help = "Say 'Alexa' to start!";
#else
        char *wake_help = "Ready!";
#endif

        if (ld == NULL) {
            ESP_LOGE(TAG, "lv_disp_t ld is NULL!!!!");
        } else {
            lvgl_port_lock(0);
            lv_label_set_text(lbl_ln3, wake_help);
            lvgl_port_unlock();
        }
    }

#ifdef CONFIG_WILLOW_USE_MULTINET
    ESP_LOGI(TAG, "cmd_multinet[] size: %u bytes", get_cmd_multinet_size());
#endif

    get_mac_address(); // should be on wifi by now; print the MAC

    ESP_LOGI(TAG, "Startup complete. Waiting for wake word.");

    ESP_ERROR_CHECK_WITHOUT_ABORT(timer_start(TIMER_GROUP_0, TIMER_0));

#ifdef CONFIG_WILLOW_DEBUG_RUNTIME_STATS
    xTaskCreate(&task_debug_runtime_stats, "dbg_runtime_stats", 4 * 1024, NULL, 0, NULL);
#endif

    while (true) {
#ifdef CONFIG_WILLOW_DEBUG_MEM
        printf("MALLOC_CAP_INTERNAL:\n");
        heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
        printf("MALLOC_CAP_SPIRAM:\n");
        heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
#endif
#ifdef CONFIG_WILLOW_DEBUG_TASKS
        char buf[128];
        vTaskList(&buf);
        printf("%s\n", buf);
#endif
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
