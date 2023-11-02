/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_err.h"
#include "bsp/esp-box-3.h"
#include "bsp_err_check.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "ESP-BOX-3";

/* This configuration is used by default in bsp_audio_init() */
#define BSP_I2S_DUPLEX_MONO_CFG(_sample_rate)                      \
    {                                                                 \
        .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX,          \
        .sample_rate = _sample_rate,                                  \
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,                 \
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,                  \
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,            \
        .dma_buf_count = 3,                                           \
        .dma_buf_len = 1024,                                          \
        .use_apll = true,                                             \
        .tx_desc_auto_clear = true,                                   \
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_IRAM \
    }

static const audio_codec_data_if_t *i2s_data_if = NULL;  /* Codec data interface */

esp_err_t bsp_audio_init(const i2s_config_t *i2s_config)
{
    esp_err_t ret = ESP_FAIL;

    if (i2s_data_if != NULL) {
        /* Audio was initialized before */
        return ESP_OK;
    }

    /* Setup I2S peripheral */
    const i2s_pin_config_t i2s_pin_config = {
        .mck_io_num = BSP_I2S_MCLK,
        .bck_io_num = BSP_I2S_SCLK,
        .ws_io_num = BSP_I2S_LCLK,
        .data_out_num = BSP_I2S_DOUT,
        .data_in_num = BSP_I2S_DSIN
    };

    /* Setup I2S channels */
    const i2s_config_t std_cfg_default = BSP_I2S_DUPLEX_MONO_CFG(22050);
    const i2s_config_t *p_i2s_cfg = &std_cfg_default;
    if (i2s_config != NULL) {
        p_i2s_cfg = i2s_config;
    }

    ESP_ERROR_CHECK(i2s_driver_install(CONFIG_BSP_I2S_NUM, p_i2s_cfg, 0, NULL));
    ESP_GOTO_ON_ERROR(i2s_set_pin(CONFIG_BSP_I2S_NUM, &i2s_pin_config), err, TAG, "I2S set pin failed");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = CONFIG_BSP_I2S_NUM,
    };
    i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    BSP_NULL_CHECK_GOTO(i2s_data_if, err);

    return ESP_OK;

err:
    i2s_driver_uninstall(CONFIG_BSP_I2S_NUM);
    return ret;
}

const audio_codec_data_if_t *bsp_audio_get_codec_itf(void)
{
    return i2s_data_if;
}
