/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_spiffs.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_vfs_fat.h"

#include "iot_button.h"
#include "bsp/esp-box-3.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_lcd_touch_tt21100.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_ili9341.h"
#include "esp_lvgl_port.h"
#include "bsp_err_check.h"
#include "esp_codec_dev_defaults.h"

#include "i2c_bus.h"
#include "audio_hal.h"
#include "board_def.h"
#include "board_pins_config.h"
#include "esp_peripherals.h"

static const char *TAG = "ESP-BOX-3";

/** @cond */
_Static_assert(CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS > 0, "Touch buttons must be supported for this BSP");
/** @endcond */

static const ili9341_lcd_init_cmd_t vendor_specific_init[] = {
    {0xC8, (uint8_t []){0xFF, 0x93, 0x42}, 3, 0},
    {0xC0, (uint8_t []){0x0E, 0x0E}, 2, 0},
    {0xC5, (uint8_t []){0xD0}, 1, 0},
    {0xC1, (uint8_t []){0x02}, 1, 0},
    {0xB4, (uint8_t []){0x02}, 1, 0},
    {0xE0, (uint8_t []){0x00, 0x03, 0x08, 0x06, 0x13, 0x09, 0x39, 0x39, 0x48, 0x02, 0x0a, 0x08, 0x17, 0x17, 0x0F}, 15, 0},
    {0xE1, (uint8_t []){0x00, 0x28, 0x29, 0x01, 0x0d, 0x03, 0x3f, 0x33, 0x52, 0x04, 0x0f, 0x0e, 0x37, 0x38, 0x0F}, 15, 0},

    {0xB1, (uint8_t []){00, 0x1B}, 2, 0},
    {0x36, (uint8_t []){0x08}, 1, 0},
    {0xB6, (uint8_t []){0x0A, 0xE0}, 2, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0xB7, (uint8_t []){0x06}, 1, 0},

    {0x11, (uint8_t []){0}, 0x80, 0},
    /* Display on */
    {0x29, (uint8_t []){0}, 0x80, 0},

    {0, (uint8_t []){0}, 0xff, 0},
};

static lv_disp_t *disp;
static lv_indev_t *disp_indev = NULL;
static esp_lcd_touch_handle_t tp;   // LCD touch handle
static esp_lcd_panel_handle_t panel_handle = NULL;

sdmmc_card_t *bsp_sdcard = NULL;    // Global SD card handler
static bool i2c_initialized = false;

// This is just a wrapper to get function signature for espressif/button API callback
static uint8_t bsp_get_main_button(void *param);
static esp_err_t bsp_init_main_button(void *param);

static const button_config_t bsp_button_config[BSP_BUTTON_NUM] = {
    {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config.gpio_num = BSP_BUTTON_CONFIG_IO,
        .gpio_button_config.active_level = 0,
    },
    {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config.gpio_num = BSP_BUTTON_MUTE_IO,
        .gpio_button_config.active_level = 0,
    },
    {
        .type = BUTTON_TYPE_CUSTOM,
        .custom_button_config.button_custom_init = bsp_init_main_button,
        .custom_button_config.button_custom_get_key_value = bsp_get_main_button,
        .custom_button_config.button_custom_deinit = NULL,
        .custom_button_config.active_level = 1,
        .custom_button_config.priv = (void *) BSP_BUTTON_MAIN,
    }
};

esp_err_t bsp_i2c_init(void)
{
    /* I2C was initialized before */
    if (i2c_initialized) {
        return ESP_OK;
    }

    // const i2c_config_t i2c_conf = {
    //     .mode = I2C_MODE_MASTER,
    //     .sda_io_num = BSP_I2C_SDA,
    //     .sda_pullup_en = GPIO_PULLUP_DISABLE,
    //     .scl_io_num = BSP_I2C_SCL,
    //     .scl_pullup_en = GPIO_PULLUP_DISABLE,
    //     .master.clk_speed = CONFIG_BSP_I2C_CLK_SPEED_HZ
    // };
    // BSP_ERROR_CHECK_RETURN_ERR(i2c_param_config(BSP_I2C_NUM, &i2c_conf));
    // BSP_ERROR_CHECK_RETURN_ERR(i2c_driver_install(BSP_I2C_NUM, i2c_conf.mode, 0, 0, 0));

    i2c_config_t es_i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    get_i2c_pins(BSP_I2C_NUM, &es_i2c_cfg);
    ESP_LOGI(TAG, "sda_io_num: %d, scl_io_num: %d", es_i2c_cfg.sda_io_num, es_i2c_cfg.scl_io_num);
    i2c_bus_create(BSP_I2C_NUM, &es_i2c_cfg);

    i2c_initialized = true;

    return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void)
{
    BSP_ERROR_CHECK_RETURN_ERR(i2c_driver_delete(BSP_I2C_NUM));
    i2c_initialized = false;
    return ESP_OK;
}

static esp_err_t bsp_i2c_device_probe(uint8_t addr)
{
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    if (i2c_master_cmd_begin(BSP_I2C_NUM, cmd, 1000) == ESP_OK) {
        ret = ESP_OK;
    }
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t bsp_spiffs_mount(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_BSP_SPIFFS_MOUNT_POINT,
        .partition_label = CONFIG_BSP_SPIFFS_PARTITION_LABEL,
        .max_files = CONFIG_BSP_SPIFFS_MAX_FILES,
#ifdef CONFIG_BSP_SPIFFS_FORMAT_ON_MOUNT_FAIL
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
    };

    esp_err_t ret_val = esp_vfs_spiffs_register(&conf);

    BSP_ERROR_CHECK_RETURN_ERR(ret_val);

    size_t total = 0, used = 0;
    ret_val = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret_val != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret_val));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ret_val;
}

esp_err_t bsp_spiffs_unmount(void)
{
    return esp_vfs_spiffs_unregister(CONFIG_BSP_SPIFFS_PARTITION_LABEL);
}

esp_err_t bsp_sdcard_mount(void)
{
    gpio_config_t power_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BSP_SD_POWER
    };
    ESP_ERROR_CHECK(gpio_config(&power_gpio_config));

    /* SD card power on first */
    ESP_ERROR_CHECK(gpio_set_level(BSP_SD_POWER, 0));

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.cmd = BSP_SD_CMD;
    slot_config.clk = BSP_SD_CLK;
    slot_config.d0 = BSP_SD_D0;
    slot_config.d1 = BSP_SD_D1;
    slot_config.d2 = BSP_SD_D2;
    slot_config.d3 = BSP_SD_D3;

    return esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &bsp_sdcard);
}

esp_err_t bsp_sdcard_unmount(void)
{
    return esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, bsp_sdcard);
}

esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void)
{
    return NULL;

    // const audio_codec_data_if_t *i2s_data_if = bsp_audio_get_codec_itf();
    // if (i2s_data_if == NULL) {
    //     /* Initilize I2C */
    //     BSP_ERROR_CHECK_RETURN_ERR(bsp_i2c_init());
    //     /* Configure I2S peripheral and Power Amplifier */
    //     BSP_ERROR_CHECK_RETURN_ERR(bsp_audio_init(NULL));
    //     i2s_data_if = bsp_audio_get_codec_itf();
    // }
    // assert(i2s_data_if);

    // const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    // audio_codec_i2c_cfg_t i2c_cfg = {
    //     .port = BSP_I2C_NUM,
    //     .addr = ES8311_CODEC_DEFAULT_ADDR,
    // };
    // const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    // BSP_NULL_CHECK(i2c_ctrl_if, NULL);

    // esp_codec_dev_hw_gain_t gain = {
    //     .pa_voltage = 5.0,
    //     .codec_dac_voltage = 3.3,
    // };

    // es8311_codec_cfg_t es8311_cfg = {
    //     .ctrl_if = i2c_ctrl_if,
    //     .gpio_if = gpio_if,
    //     .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
    //     .pa_pin = BSP_POWER_AMP_IO,
    //     .pa_reverted = false,
    //     .master_mode = false,
    //     .use_mclk = true,
    //     .digital_mic = false,
    //     .invert_mclk = false,
    //     .invert_sclk = false,
    //     .hw_gain = gain,
    // };
    // const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
    // BSP_NULL_CHECK(es8311_dev, NULL);

    // esp_codec_dev_cfg_t codec_dev_cfg = {
    //     .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    //     .codec_if = es8311_dev,
    //     .data_if = i2s_data_if,
    // };
    // return esp_codec_dev_new(&codec_dev_cfg);
}

esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void)
{
    return NULL;

    // const audio_codec_data_if_t *i2s_data_if = bsp_audio_get_codec_itf();
    // if (i2s_data_if == NULL) {
    //     /* Initilize I2C */
    //     BSP_ERROR_CHECK_RETURN_ERR(bsp_i2c_init());
    //     /* Configure I2S peripheral and Power Amplifier */
    //     BSP_ERROR_CHECK_RETURN_ERR(bsp_audio_init(NULL));
    //     i2s_data_if = bsp_audio_get_codec_itf();
    // }
    // assert(i2s_data_if);

    // audio_codec_i2c_cfg_t i2c_cfg = {
    //     .port = BSP_I2C_NUM,
    //     .addr = ES7210_CODEC_DEFAULT_ADDR,
    // };
    // const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    // BSP_NULL_CHECK(i2c_ctrl_if, NULL);

    // es7210_codec_cfg_t es7210_cfg = {
    //     .ctrl_if = i2c_ctrl_if,
    // };
    // const audio_codec_if_t *es7210_dev = es7210_codec_new(&es7210_cfg);
    // BSP_NULL_CHECK(es7210_dev, NULL);

    // esp_codec_dev_cfg_t codec_es7210_dev_cfg = {
    //     .dev_type = ESP_CODEC_DEV_TYPE_IN,
    //     .codec_if = es7210_dev,
    //     .data_if = i2s_data_if,
    // };
    // return esp_codec_dev_new(&codec_es7210_dev_cfg);
}

// Bit number used to represent command and parameter
#define LCD_CMD_BITS           8
#define LCD_PARAM_BITS         8
#define LCD_LEDC_CH            CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH

static esp_err_t bsp_display_brightness_init(void)
{
    // Setup LEDC peripheral for PWM backlight control
    const ledc_channel_config_t LCD_backlight_channel = {
        .gpio_num = BSP_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = 1,
        .duty = 0,
        .hpoint = 0
    };
    const ledc_timer_config_t LCD_backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = 1,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };

    BSP_ERROR_CHECK_RETURN_ERR(ledc_timer_config(&LCD_backlight_timer));
    BSP_ERROR_CHECK_RETURN_ERR(ledc_channel_config(&LCD_backlight_channel));

    return ESP_OK;
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    if (brightness_percent < 0) {
        brightness_percent = 0;
    }

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);
    uint32_t duty_cycle = (1023 * brightness_percent) / 100; // LEDC resolution set to 10bits, thus: 100% = 1023
    BSP_ERROR_CHECK_RETURN_ERR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle));
    BSP_ERROR_CHECK_RETURN_ERR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));

    return ESP_OK;
}

esp_err_t bsp_display_backlight_off(void)
{
    return bsp_display_brightness_set(0);
}

esp_err_t bsp_display_backlight_on(void)
{
    return bsp_display_brightness_set(100);
}

static esp_err_t bsp_lcd_enter_sleep(void)
{
    assert(panel_handle);
    return esp_lcd_panel_disp_on_off(panel_handle, false);
}

static esp_err_t bsp_lcd_exit_sleep(void)
{
    assert(panel_handle);
    return esp_lcd_panel_disp_on_off(panel_handle, true);
}

esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;
    assert(config != NULL && config->max_transfer_sz > 0);

    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "Brightness init failed");

    /* Initilize I2C */
    BSP_ERROR_CHECK_RETURN_ERR(bsp_i2c_init());

    ESP_LOGD(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = BSP_LCD_PCLK,
        .mosi_io_num = BSP_LCD_DATA0,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = config->max_transfer_sz,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI init failed");

    ESP_LOGD(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = BSP_LCD_DC,
        .cs_gpio_num = BSP_LCD_CS,
        .pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_config, ret_io), err, TAG, "New panel IO failed");

    ESP_LOGD(TAG, "Install LCD driver");
    const ili9341_vendor_config_t vendor_config = {
        .init_cmds = &vendor_specific_init[0],
        .init_cmds_size = sizeof(vendor_specific_init) / sizeof(ili9341_lcd_init_cmd_t),
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST, // Shared with Touch reset
        .flags.reset_active_high = 1,
        .color_space = BSP_LCD_COLOR_SPACE,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .vendor_config = (void *)&vendor_config,
    };

    if (ESP_OK == bsp_i2c_device_probe(ESP_LCD_TOUCH_IO_I2C_TT21100_ADDRESS)) {
        ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7789(*ret_io, &panel_config, ret_panel), err, TAG, "New panel failed");
    } else {
        ESP_GOTO_ON_ERROR(esp_lcd_new_panel_ili9341(*ret_io, &panel_config, ret_panel), err, TAG, "New panel failed");
    }

    esp_lcd_panel_reset(*ret_panel);
    esp_lcd_panel_init(*ret_panel);
    // esp_lcd_panel_mirror(*ret_panel, true, true);
    return ret;

err:
    if (*ret_panel) {
        esp_lcd_panel_del(*ret_panel);
    }
    if (*ret_io) {
        esp_lcd_panel_io_del(*ret_io);
    }
    spi_bus_free(BSP_LCD_SPI_NUM);
    return ret;
}

static lv_disp_t *bsp_display_lcd_init(void)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const bsp_display_config_t bsp_disp_cfg = {
        .max_transfer_sz = (BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT) * sizeof(uint16_t),
    };
    BSP_ERROR_CHECK_RETURN_NULL(bsp_display_new(&bsp_disp_cfg, &panel_handle, &io_handle));

    esp_lcd_panel_disp_on_off(panel_handle, true);

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
#if CONFIG_BSP_LCD_DRAW_BUF_DOUBLE
        .double_buffer = 1,
#else
        .double_buffer = 0,
#endif
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = false,
            .mirror_x = true,
            .mirror_y = true,
        },
        .flags = {
            .buff_dma = true,
        }
    };

    return lvgl_port_add_disp(&disp_cfg);
}

__attribute__((weak)) esp_err_t esp_lcd_touch_enter_sleep(esp_lcd_touch_handle_t tp)
{
    ESP_LOGE(TAG, "Sleep mode not supported!");
    return ESP_FAIL;
}

__attribute__((weak)) esp_err_t esp_lcd_touch_exit_sleep(esp_lcd_touch_handle_t tp)
{
    ESP_LOGE(TAG, "Sleep mode not supported!");
    return ESP_FAIL;
}

static esp_err_t bsp_touch_enter_sleep(void)
{
    assert(tp);
    return esp_lcd_touch_enter_sleep(tp);
}

static esp_err_t bsp_touch_exit_sleep(void)
{
    assert(tp);
    return esp_lcd_touch_exit_sleep(tp);
}

esp_err_t bsp_touch_new(const bsp_touch_config_t *config, esp_lcd_touch_handle_t *ret_touch)
{
    /* Initilize I2C */
    BSP_ERROR_CHECK_RETURN_ERR(bsp_i2c_init());

    /* Initialize touch */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC, // Shared with LCD reset
        .int_gpio_num = BSP_LCD_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config;

    if (ESP_OK == bsp_i2c_device_probe(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS)) {
        esp_lcd_panel_io_i2c_config_t config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        memcpy(&tp_io_config, &config, sizeof(config));
    } else if (ESP_OK == bsp_i2c_device_probe(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP)) {
        esp_lcd_panel_io_i2c_config_t config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        memcpy(&tp_io_config, &config, sizeof(config));
    } else if (ESP_OK == bsp_i2c_device_probe(ESP_LCD_TOUCH_IO_I2C_TT21100_ADDRESS)) {
        esp_lcd_panel_io_i2c_config_t config = ESP_LCD_TOUCH_IO_I2C_TT21100_CONFIG();
        memcpy(&tp_io_config, &config, sizeof(config));
        tp_cfg.flags.mirror_x = 1;
    } else {
        ESP_LOGE(TAG, "Touch not found");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)BSP_I2C_NUM, &tp_io_config, &tp_io_handle), TAG, "");
    if (ESP_LCD_TOUCH_IO_I2C_TT21100_ADDRESS == tp_io_config.dev_addr) {
        ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_tt21100(tp_io_handle, &tp_cfg, ret_touch), TAG, "New tt21100 failed");
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, ret_touch), TAG, "New gt911 failed");
    }

    return ESP_OK;
}

static lv_indev_t *bsp_display_indev_init(lv_disp_t *disp)
{
    BSP_ERROR_CHECK_RETURN_NULL(bsp_touch_new(NULL, &tp));
    assert(tp);

    /* Add touch input (for selected screen) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
    };

    return lvgl_port_add_touch(&touch_cfg);
}

lv_disp_t *bsp_display_start(void)
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG()
    };
    return bsp_display_start_with_config(&cfg);
}

lv_disp_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg)
{
    assert(cfg != NULL);
    BSP_ERROR_CHECK_RETURN_NULL(lvgl_port_init(&cfg->lvgl_port_cfg));

    BSP_ERROR_CHECK_RETURN_NULL(bsp_display_brightness_init());

    BSP_NULL_CHECK(disp = bsp_display_lcd_init(), NULL);

    BSP_NULL_CHECK(disp_indev = bsp_display_indev_init(disp), NULL);

    return disp;
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    return disp_indev;
}

void bsp_display_rotate(lv_disp_t *disp, lv_disp_rot_t rotation)
{
    lv_disp_set_rotation(disp, rotation);
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}

esp_err_t bsp_display_enter_sleep(void)
{
    BSP_ERROR_CHECK_RETURN_ERR(bsp_lcd_enter_sleep());
    BSP_ERROR_CHECK_RETURN_ERR(bsp_display_backlight_off());
    BSP_ERROR_CHECK_RETURN_ERR(bsp_touch_enter_sleep());
    return ESP_OK;
}

esp_err_t bsp_display_exit_sleep(void)
{
    BSP_ERROR_CHECK_RETURN_ERR(bsp_lcd_exit_sleep());
    BSP_ERROR_CHECK_RETURN_ERR(bsp_display_backlight_on());
    BSP_ERROR_CHECK_RETURN_ERR(bsp_touch_exit_sleep());
    return ESP_OK;
}

static uint8_t bsp_get_main_button(void *param)
{
    assert(tp);
#if (CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS > 0)
    uint8_t home_btn_val = 0x00;
    esp_lcd_touch_get_button_state(tp, 0, &home_btn_val);
    return home_btn_val ? true : false;
#else
    ESP_LOGE(TAG, "Button main is inaccessible");
    return false;
#endif
}

static esp_err_t bsp_init_main_button(void *param)
{
    if (tp == NULL) {
        BSP_ERROR_CHECK_RETURN_ERR(bsp_touch_new(NULL, &tp));
    }
    return ESP_OK;
}

esp_err_t bsp_iot_button_create(button_handle_t btn_array[], int *btn_cnt, int btn_array_size)
{
    esp_err_t ret = ESP_OK;
    if ((btn_array_size < BSP_BUTTON_NUM) ||
            (btn_array == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (btn_cnt) {
        *btn_cnt = 0;
    }
    for (int i = 0; i < BSP_BUTTON_NUM; i++) {
        btn_array[i] = iot_button_create(&bsp_button_config[i]);
        if (btn_array[i] == NULL) {
            ret = ESP_FAIL;
            break;
        }
        if (btn_cnt) {
            (*btn_cnt)++;
        }
    }
    return ret;
}
