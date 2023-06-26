#include "amrwb_encoder.h"
#include "audio_hal.h"
#include "audio_mem.h"
#include "audio_pipeline.h"
#include "audio_recorder.h"
#include "audio_thread.h"
#include "board.h"
#include "es7210.h"
#include "esp_decoder.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_task_wdt.h"
#include "filter_resample.h"
#include "flac_decoder.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "lvgl.h"
#include "model_path.h"
#include "raw_stream.h"
#include "recorder_encoder.h"
#include "recorder_sr.h"
#include "sdkconfig.h"
#include "spiffs_stream.h"
#include "wav_encoder.h"

#include "audio.h"
#include "config.h"
#include "display.h"
#include "slvgl.h"
#include "timer.h"
#include "ui.h"

#include "endpoint/hass.h"
#include "endpoint/openhab.h"
#include "endpoint/rest.h"

#include "generated_cmd_multinet.h"

#define MULTINET_TWDT   30
#define WIS_URL_TTS_ARG "?speaker=CLB&text="

QueueHandle_t q_rec;
bool recording = false;
static audio_element_handle_t hdl_ae_hs, hdl_ae_rs_from_i2s, hdl_ae_rs_to_api = NULL;
static audio_pipeline_handle_t hdl_ap, hdl_ap_to_api;
static audio_rec_handle_t hdl_ar = NULL;
static audio_thread_t hdl_at = NULL;
static bool stream_to_api = false;
static const char *TAG = "WILLOW/AUDIO";
static int total_write = 0;

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

static void init_audio_response(void)
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

static void init_esp_audio(audio_board_handle_t hdl)
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
    esp_audio_vol_set(hdl_ea, config_get_int("speaker_volume"));
    ESP_LOGI(TAG, "audio player initialized");
}

static esp_err_t cb_ar_event(audio_rec_evt_t are, void *data)
{
    int msg = -1;
    int command_id = 0;

    switch (are) {
        case AUDIO_REC_VAD_END:
            ESP_LOGI(TAG, "AUDIO_REC_VAD_END");
            if (esp_timer_is_active(hdl_sess_timer)) {
                esp_timer_stop(hdl_sess_timer);
            }
            break;
        case AUDIO_REC_VAD_START:
            ESP_LOGI(TAG, "AUDIO_REC_VAD_START");
            if (strcmp(config_get_char("speech_rec_mode"), "Multinet") == 0) {
                msg = MSG_START_LOCAL;
            } else if (strcmp(config_get_char("speech_rec_mode"), "WIS") == 0) {
                msg = MSG_START;
            } else {
                return ESP_ERR_INVALID_ARG;
            }
            xQueueSend(q_rec, &msg, 0);
            break;
        case AUDIO_REC_COMMAND_DECT:
            // Multinet timeout
            ESP_LOGI(TAG, "AUDIO_REC_COMMAND_DECT");
            war.fn_err("unrecognized command");
            lvgl_port_lock(0);
            lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);

            lv_label_set_text(lbl_ln4, "#ff0000 Unrecognized Command");
            lvgl_port_unlock();
            reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, false);
            break;
        case AUDIO_REC_WAKEUP_END:
            ESP_LOGI(TAG, "AUDIO_REC_WAKEUP_END");
            msg = MSG_STOP;
            xQueueSend(q_rec, &msg, 0);
            break;
        case AUDIO_REC_WAKEUP_START:
            ESP_LOGI(TAG, "AUDIO_REC_WAKEUP_START");
            if (recording) {
                msg = MSG_STOP;
                xQueueSend(q_rec, &msg, 0);
                break;
            }
            reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, true);
            lvgl_port_lock(0);
            lv_obj_add_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_ln5, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(btn_cancel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);

            if (strcmp(config_get_char("speech_rec_mode"), "Multinet") == 0) {
                lv_label_set_text_static(lbl_ln3, "Say local command...");
            } else if (strcmp(config_get_char("speech_rec_mode"), "WIS") == 0) {
                lv_label_set_text_static(lbl_ln3, "Say command...");
                reset_timer(hdl_sess_timer, config_get_int("stream_timeout") * 1000 * 1000, false);
            } else {
                return ESP_ERR_INVALID_ARG;
            }

            lv_obj_add_event_cb(btn_cancel, cb_btn_cancel, LV_EVENT_PRESSED, NULL);
            lvgl_port_unlock();
            display_set_backlight(true);
            break;
        default:
            if (strcmp(config_get_char("speech_rec_mode"), "Multinet") == 0) {
                // Catch all for local commands
                command_id = are;
                char *json;
                json = calloc(sizeof(char), 29 + strlen(lookup_cmd_multinet(command_id)));
                snprintf(json, 29 + strlen(lookup_cmd_multinet(command_id)), "{\"text\":\"%s\",\"language\":\"en\"}",
                         lookup_cmd_multinet(command_id));
                if (strcmp(config_get_char("command_endpoint"), "Home Assistant") == 0) {
                    hass_send(json);
                } else if (strcmp(config_get_char("command_endpoint"), "openHAB") == 0) {
                    openhab_send(lookup_cmd_multinet(command_id));
                } else if (strcmp(config_get_char("command_endpoint"), "REST") == 0) {
                    rest_send(json);
                }
                free(json);

                ESP_LOGI(TAG, "Got local command ID: '%d'", command_id);
                lvgl_port_lock(0);
                lv_obj_clear_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);

                lv_label_set_text_static(lbl_ln1, "I heard command:");
                lv_label_set_text(lbl_ln2, lookup_cmd_multinet(command_id));
                lvgl_port_unlock();
                reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, false);
            } else {
                ESP_LOGI(TAG, "cb_ar_event: unhandled event: '%d'", are);
            }
            break;
    }

    return ESP_OK;
}

static int feed_afe(int16_t *buf, int len, void *ctx, TickType_t ticks)
{
    if (buf == NULL || hdl_ae_rs_from_i2s == NULL) {
        return -1;
    }

    return raw_stream_read(hdl_ae_rs_from_i2s, (char *)buf, len);
}

static esp_err_t hdl_ev_hs(http_stream_event_msg_t *msg)
{
    if (msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_handle_t http = (esp_http_client_handle_t)msg->http_client;
    char len_buf[16];
    int wlen = 0;

    switch (msg->event_id) {
        case HTTP_STREAM_PRE_REQUEST:
            ESP_LOGI(TAG, "WIS HTTP client starting stream, waiting for end of speech");
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
            if (strcmp(config_get_char("audio_codec"), "AMR-WB") == 0) {
                esp_http_client_set_header(http, "x-audio-codec", "amrwb");
            } else if (strcmp(config_get_char("audio_codec"), "WAV") == 0) {
                esp_http_client_set_header(http, "x-audio-codec", "wav");
            } else if (strcmp(config_get_char("audio_codec"), "PCM") == 0) {
                esp_http_client_set_header(http, "x-audio-codec", "pcm");
            }
            total_write = 0;
            return ESP_OK;

        case HTTP_STREAM_ON_REQUEST:
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
            // ESP_LOGI(TAG, "WIS HTTP client total bytes written: %d", total_write);
            return msg->buffer_len;

        case HTTP_STREAM_POST_REQUEST:
            ESP_LOGI(TAG, "WIS HTTP client HTTP_STREAM_POST_REQUEST, write end chunked marker");
            if (esp_http_client_write(http, "0\r\n\r\n", 5) <= 0) {
                return ESP_FAIL;
            }
            return ESP_OK;

        case HTTP_STREAM_FINISH_REQUEST:
            ESP_LOGI(TAG, "WIS HTTP client HTTP_STREAM_FINISH_REQUEST");
            // Check status code
            int http_status = esp_http_client_get_status_code(http);
            if (http_status != 200) {
                if (http_status == 406) {
                    ESP_LOGE(TAG, "WIS returned Unauthorized Speaker");
                    lvgl_port_lock(0);
                    lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text_static(lbl_ln4, "Unauthorized Speaker");
                    lvgl_port_unlock();
                    war.fn_err("Unauthorized Speaker");
                }
                ESP_LOGE(TAG, "WIS returned HTTP error: %d", http_status);
                return ESP_FAIL;
            }
            // Allocate memory for response. Should be enough?
            char *buf = calloc(sizeof(char), 2048);
            assert(buf);
            int read_len = esp_http_client_read(http, buf, 2048);
            if (read_len <= 0) {
                free(buf);
                return ESP_FAIL;
            }
            buf[read_len] = 0;
            ESP_LOGI(TAG, "WIS HTTP Response = %s", (char *)buf);
            if (strcmp(config_get_char("command_endpoint"), "Home Assistant") == 0) {
                hass_send(buf);
            } else if (strcmp(config_get_char("command_endpoint"), "openHAB") == 0) {
                openhab_send(buf);
            } else if (strcmp(config_get_char("command_endpoint"), "REST") == 0) {
                rest_send(buf);
            }

            cJSON *cjson = cJSON_Parse(buf);
            cJSON *text = cJSON_GetObjectItemCaseSensitive(cjson, "text");
            cJSON *speaker_status = cJSON_GetObjectItemCaseSensitive(cjson, "speaker_status");

            lvgl_port_lock(0);
            lv_obj_clear_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
            if (cJSON_IsString(speaker_status) && speaker_status->valuestring != NULL) {
                lv_label_set_text(lbl_ln1, speaker_status->valuestring);
            } else {
                lv_label_set_text(lbl_ln1, "I heard:");
            }
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

static esp_err_t init_ap_to_api(void)
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
    audio_element_set_uri(hdl_ae_hs, config_get_char("wis_url"));

    audio_element_info_t info = AUDIO_ELEMENT_INFO_DEFAULT();
    audio_element_getinfo(hdl_ae_hs, &info);
    ESP_LOGI(TAG, "audio_element_getinfo(hdl_ae_hs): sample_rate='%d' channels='%d' bits='%d' bps = '%d'",
             info.sample_rates, info.channels, info.bits, info.bps);

    return ESP_OK;
}

static void start_rec(void)
{
    audio_element_handle_t hdl_ae_is;
    audio_pipeline_cfg_t cfg_ap = DEFAULT_AUDIO_PIPELINE_CONFIG();

    hdl_ap = audio_pipeline_init(&cfg_ap);
    if (hdl_ap == NULL) {
        return;
    }

    i2s_stream_cfg_t cfg_is = {
        .expand_src_bits = I2S_BITS_PER_SAMPLE_16BIT,
        .i2s_config = {
            .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .dma_buf_count = 3,
            .dma_buf_len = 300,
            .fixed_mclk = 0,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_IRAM,
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
            .sample_rate = 44100,
            .tx_desc_auto_clear = true,
            .use_apll = false,	// not supported on ESP32-S3-BOX
        },
        .i2s_port = CODEC_ADC_I2S_PORT,
        .multi_out_num = 0,
        .need_expand = false,
        .out_rb_size = 8 * 1024, // default is 8 * 1024
        .stack_in_ext = false,
        .task_core = I2S_STREAM_TASK_CORE,
        .task_prio = I2S_STREAM_TASK_PRIO,
        .task_stack = I2S_STREAM_TASK_STACK,
        .type = AUDIO_STREAM_READER,
        .uninstall_drv = true,
        .use_alc = false,
        .volume = 0,
    };
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

    int wakenet_mode = -1;
    if (strcmp(config_get_char("wake_mode"), "2CH_90") == 0) {
        wakenet_mode = DET_MODE_2CH_90;
    } else if (strcmp(config_get_char("wake_mode"), "2CH_95") == 0) {
        wakenet_mode = DET_MODE_2CH_95;
    } else if (strcmp(config_get_char("wake_mode"), "1CH_90") == 0) {
        wakenet_mode = DET_MODE_90;
    } else if (strcmp(config_get_char("wake_mode"), "1CH_95") == 0) {
        wakenet_mode = DET_MODE_95;
    } else if (strcmp(config_get_char("wake_mode"), "3CH_90") == 0) {
        wakenet_mode = DET_MODE_3CH_90;
    } else if (strcmp(config_get_char("wake_mode"), "3CH_95") == 0) {
        wakenet_mode = DET_MODE_3CH_95;
    }

    afe_config_t cfg_afe = {
        .aec_init = true,
        .se_init = true,
        .vad_init = true,
        .wakenet_init = true,
        .voice_communication_init = false,
        .voice_communication_agc_init = false,
        .voice_communication_agc_gain = 15,
        .vad_mode = config_get_int("vad_mode"),
        .wakenet_mode = wakenet_mode,
        .wakenet_model_name = NULL,
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

    ESP_LOGI(TAG, "Using record buffer '%d'", config_get_int("record_buffer"));
    cfg_srr.rb_size = config_get_int("record_buffer") * 1024;

    if (strcmp(config_get_char("speech_rec_mode"), "Multinet") == 0) {
        ESP_LOGI(TAG, "Using local multinet");
        ESP_LOGI(TAG, "cmd_multinet[] size: %u bytes", get_cmd_multinet_size());
        esp_task_wdt_init(MULTINET_TWDT, CONFIG_TASK_WDT_PANIC ? true : false);
        cfg_srr.multinet_init = true;
        cfg_srr.rb_size = 6 * 1024;
    }

    recorder_encoder_cfg_t recorder_encoder_cfg = {0};

    if (strcmp(config_get_char("audio_codec"), "AMR-WB") == 0) {
        amrwb_encoder_cfg_t amrwb_cfg = DEFAULT_AMRWB_ENCODER_CONFIG();
        amrwb_cfg.contain_amrwb_header = true;
        amrwb_cfg.stack_in_ext = true;
        amrwb_cfg.task_core = 0;
        amrwb_cfg.task_prio = 5;
        amrwb_cfg.out_rb_size = 8 * 1024;
        amrwb_cfg.bitrate_mode = AMRWB_ENC_BITRATE_MD2385;

        recorder_encoder_cfg.encoder = amrwb_encoder_init(&amrwb_cfg);
    } else if (strcmp(config_get_char("audio_codec"), "WAV") == 0) {
        wav_encoder_cfg_t wav_cfg = DEFAULT_WAV_ENCODER_CONFIG();
        wav_cfg.stack_in_ext = true;
        wav_cfg.task_core = 0;
        wav_cfg.task_prio = 5;
        wav_cfg.out_rb_size = 8 * 1024;

        recorder_encoder_cfg.encoder = wav_encoder_init(&wav_cfg);
    }

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
        .vad_off = config_get_int("vad_timeout"),
        .wakeup_end = 1,
        .encoder_handle = NULL,
        .encoder_iface = NULL,
    };
    cfg_ar.sr_handle = recorder_sr_create(&cfg_srr, &cfg_ar.sr_iface);
    if (strcmp(config_get_char("audio_codec"), "AMR-WB") == 0 || strcmp(config_get_char("audio_codec"), "WAV") == 0) {
        ESP_LOGI(TAG, "Using recorder encoder");
        cfg_ar.encoder_handle = recorder_encoder_create(&recorder_encoder_cfg, &cfg_ar.encoder_iface);
    }
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
                    // this confirms that the URL is still set correctly
                    ESP_LOGI(TAG, "Using WIS URL '%s'", audio_element_get_uri(hdl_ae_hs));
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
                    reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, false);
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

void init_audio(void)
{
    esp_err_t ret;
    int gpio_level;

    gpio_level = gpio_get_level(GPIO_NUM_1);
    if (gpio_level == 0) {
        ESP_LOGW(TAG, "mute is activated, please unmute to continue startup");
        if (ld == NULL) {
            ESP_LOGE(TAG, "lv_disp_t ld is NULL!!!!");
        } else {
            ui_pr_err("Mute activated!", "Unmute to continue.");
        }
        while (gpio_get_level(GPIO_NUM_1) == 0) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    audio_board_handle_t hdl_audio_board = audio_board_init();
    gpio_set_level(get_pa_enable_gpio(), 0);
    ret = audio_hal_ctrl_codec(hdl_audio_board->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    ESP_LOGI(TAG, "audio_hal_ctrl_codec: %s", esp_err_to_name(ret));

    audio_hal_set_volume(hdl_audio_board->audio_hal, config_get_int("speaker_volume"));
    init_audio_response();
    init_session_timer();
    if (strcmp(config_get_char("speech_rec_mode"), "WIS") == 0) {
        init_ap_to_api();
    }
    init_esp_audio(hdl_audio_board);
    start_rec();
    es7210_adc_set_gain(config_get_int("mic_gain"));

    ESP_LOGI(TAG, "app_main() - start_rec() finished");

    q_rec = xQueueCreate(3, sizeof(int));
    audio_thread_create(&hdl_at, "at_read", at_read, NULL, 4 * 1024, 5, true, 0);

#if defined(CONFIG_WILLOW_WAKE_WORD_HIESP) || defined(CONFIG_SR_WN_WN9_HIESP)
    char *wake_help = "Say 'Hi ESP' to start!";
#elif defined(CONFIG_WILLOW_WAKE_WORD_ALEXA) || defined(CONFIG_SR_WN_WN9_ALEXA)
    char *wake_help = "Say 'Alexa' to start!";
#elif defined(CONFIG_WILLOW_WAKE_WORD_HILEXIN) || defined(CONFIG_SR_WN_WN9_HILEXIN)
    char *wake_help = "Say 'Hi Lexin' to start!";
#else
    char *wake_help = "Ready!";
#endif

    if (ld == NULL) {
        ESP_LOGE(TAG, "lv_disp_t ld is NULL!!!!");
    } else {
        lvgl_port_lock(0);
        lv_obj_add_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(lbl_ln4, LV_ALIGN_TOP_LEFT, 0, 120);
        lv_obj_set_width(lbl_ln5, 320);
        lv_label_set_long_mode(lbl_ln1, LV_LABEL_LONG_SCROLL);
        lv_label_set_long_mode(lbl_ln5, LV_LABEL_LONG_SCROLL);
        lv_label_set_text(lbl_ln3, wake_help);

        lvgl_port_unlock();
    }
}

void deinit_audio(void)
{
    if (hdl_at != NULL) {
        vTaskDelete(hdl_at);
    }
    if (hdl_ap != NULL) {
        audio_pipeline_stop(hdl_ap);
    }
    if (strcmp(config_get_char("speech_rec_mode"), "WIS") == 0 && hdl_ap_to_api != NULL) {
        audio_pipeline_stop(hdl_ap_to_api);
    }
    if (hdl_ea != NULL) {
        esp_audio_destroy(hdl_ea);
    }
}
