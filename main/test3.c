#include "audio_hal.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "board.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_peripherals.h"
#include "esp_wifi.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "nvs_flash.h"
#include "periph_wifi.h"
#include "sdkconfig.h"

static const char *TAG = "SALLOW_TEST3";
static int total_write = 0;

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
            snprintf(dat, sizeof(dat), "%d", 48000);
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
            // printf("\033[A\33[2K\rTotal bytes written: %d\n", total_write);
            return msg->buffer_len;

        case HTTP_STREAM_POST_REQUEST:
            ESP_LOGI(TAG, "[ + ] HTTP client HTTP_STREAM_POST_REQUEST, write end chunked marker");
            if (esp_http_client_write(http, "0\r\n\r\n", 5) <= 0) {
                return ESP_FAIL;
            }
            return ESP_OK;

        case HTTP_STREAM_FINISH_REQUEST:
            ESP_LOGI(TAG, "[ + ] HTTP client HTTP_STREAM_FINISH_REQUEST");
            printf("\033[A\33[2K\rTotal bytes written: %d\n", total_write);
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

            free(buf);
            return ESP_OK;

        default:
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_VERBOSE);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    audio_element_handle_t hdl_ae_reader_i2s, hdl_ae_writer_http;
    audio_pipeline_handle_t hdl_ap;
    esp_err_t ret;
    esp_periph_set_handle_t hdl_pset;

    audio_pipeline_cfg_t cfg_ap = DEFAULT_AUDIO_PIPELINE_CONFIG();

    hdl_ap = audio_pipeline_init(&cfg_ap);
    if (hdl_ap == NULL) {
        return;
    }

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
    //gpio_set_level(get_pa_enable_gpio(), 0);
    ret = audio_hal_ctrl_codec(hdl_audio_board->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    ESP_LOGI(TAG, "audio_hal_ctrl_codec: %s", esp_err_to_name(ret));

    i2s_stream_cfg_t cfg_is = I2S_STREAM_CFG_DEFAULT();
    cfg_is.i2s_config.use_apll = 0;     // not supported on ESP32-S3-BOX
    cfg_is.i2s_port = 0;
    cfg_is.type = AUDIO_STREAM_READER;
    hdl_ae_reader_i2s = i2s_stream_init(&cfg_is);

    audio_element_info_t aei = AUDIO_ELEMENT_INFO_DEFAULT();
    aei.bits = 16;
    aei.channels = 1;
    aei.sample_rates = 16000;
    audio_element_setinfo(hdl_ae_reader_i2s, &aei);

    http_stream_cfg_t cfg_hs = HTTP_STREAM_CFG_DEFAULT();
    cfg_hs.event_handle = hdl_ev_hs;
    cfg_hs.type = AUDIO_STREAM_WRITER;
    hdl_ae_writer_http = http_stream_init(&cfg_hs);

    audio_element_set_uri(hdl_ae_writer_http, CONFIG_SERVER_URI);

    audio_pipeline_register(hdl_ap, hdl_ae_reader_i2s, "i2s_stream_reader");
    audio_pipeline_register(hdl_ap, hdl_ae_writer_http, "http_stream_writer");

    const char *tag_link[2] = {"i2s_stream_reader", "http_stream_writer"};
    audio_pipeline_link(hdl_ap, &tag_link[0], 2);

    audio_pipeline_run(hdl_ap);

    audio_element_info_t info = AUDIO_ELEMENT_INFO_DEFAULT();
    audio_element_getinfo(hdl_ae_reader_i2s, &info);
    ESP_LOGI(TAG, "audio_element_getinfo(hdl_ae_reader_i2s): sample_rate='%d' channels='%d' bits='%d' bps = '%d'",
             info.sample_rates, info.channels, info.bits, info.bps);

    // wait ~5 seconds then stop stream
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    audio_element_set_ringbuf_done(hdl_ae_reader_i2s);
}