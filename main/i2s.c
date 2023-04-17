#include "driver/gpio.h"
#include "driver/i2s.h"
#include "esp_log.h"

#include "i2s.h"

#define I2S_PORT I2S_NUM_0

static const char *TAG = "SALLOW_I2S";

esp_err_t init_i2s(void)
{
    esp_err_t ret = ESP_OK;
    i2s_config_t cfg_i2s = {
        .bits_per_chan          = I2S_BITS_PER_CHAN_32BIT,
        .bits_per_sample        = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format         = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format   = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count          = 6,
        .dma_buf_len            = 160,
        .fixed_mclk             = 0,
        .intr_alloc_flags       = ESP_INTR_FLAG_LEVEL1,
        .mclk_multiple          = I2S_MCLK_MULTIPLE_DEFAULT,
        .mode                   = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX,
        .sample_rate            = 16000,
        .tx_desc_auto_clear     = true,
        .use_apll               = false,
    };

    i2s_pin_config_t pcfg_i2s = {
        .bck_io_num = GPIO_NUM_17,
        .ws_io_num = GPIO_NUM_47,
        .data_out_num = GPIO_NUM_15,
        .data_in_num = GPIO_NUM_16,
        .mck_io_num = GPIO_NUM_2,
    };

    printf("%s: initializing I2S\n", TAG);

    ret = i2s_driver_install(I2S_PORT, &cfg_i2s, 0, NULL);
    printf("%s: i2s_driver_install: %s\n", TAG, esp_err_to_name(ret));

    ret = i2s_set_pin(I2S_PORT, &pcfg_i2s);
    printf("%s: i2s_set_pin: %s\n", TAG, esp_err_to_name(ret));

    return ret;
}