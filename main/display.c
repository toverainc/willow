#include "board.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "shared.h"

#ifdef CONFIG_ESP32_S3_BOX_LITE_BOARD
#define BL_DUTY_OFF 1023
#define BL_DUTY_ON  BL_DUTY_OFF - CONFIG_WILLOW_LCD_BRIGHTNESS
#else
#define BL_DUTY_OFF 0
#define BL_DUTY_ON  CONFIG_WILLOW_LCD_BRIGHTNESS
#endif

static const char *TAG = "WILLOW/DISPLAY";

esp_err_t init_display(void)
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

void display_set_backlight(const bool on)
{
    if (on) {
        ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, BL_DUTY_ON, 0);
    } else {
        ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, BL_DUTY_OFF, 0);
    }
}