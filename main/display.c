#include "board.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "config.h"
#include "display.h"
#include "system.h"

#define DEFAULT_LCD_BRIGHTNESS 700
#define MIN_STROBE_PERIOD      20

static const char *TAG = "WILLOW/DISPLAY";
static int bl_duty_max;
static int bl_duty_off;
static int bl_duty_on;
enum willow_hw_t hw_type;

esp_err_t init_display(void)
{
    ESP_LOGD(TAG, "initializing display");

    switch (hw_type) {
        case WILLOW_HW_ESP32_S3_BOX_LITE:
            bl_duty_max = 0;
            bl_duty_off = 1023;
            bl_duty_on = bl_duty_off - config_get_int("lcd_brightness", DEFAULT_LCD_BRIGHTNESS);
            break;
        case WILLOW_HW_MAX:
            ESP_LOGW(TAG, "unsupported hardware");
            __attribute__((fallthrough));
        case WILLOW_HW_UNSUPPORTED:
            __attribute__((fallthrough));
        case WILLOW_HW_ESP32_S3_BOX:
            __attribute__((fallthrough));
        case WILLOW_HW_ESP32_S3_BOX_3:
            bl_duty_max = 1023;
            bl_duty_off = 0;
            bl_duty_on = config_get_int("lcd_brightness", DEFAULT_LCD_BRIGHTNESS);
            break;
    }

    ESP_LOGD(TAG, "bl_duty_on=%d bl_duty_off=%d", bl_duty_on, bl_duty_off);

    const ledc_channel_config_t cfg_bl_channel = {
        .channel = LEDC_CHANNEL_1,
        .duty = bl_duty_on,
        .gpio_num = LCD_CTRL_GPIO,
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

    hdl_lcd = (esp_lcd_panel_handle_t)audio_board_lcd_init(hdl_pset, NULL);
    ret = esp_lcd_panel_disp_on_off(hdl_lcd, true);
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

void display_set_backlight(const bool on, const bool max)
{
    int duty;

    if (on) {
        duty = max ? bl_duty_max : bl_duty_on;
    } else {
        duty = bl_duty_off;
    }
    ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty, 0);
}

void display_backlight_strobe_task(void *data)
{
    int period_ms = MIN_STROBE_PERIOD;
    willow_strobe_parms_t *wsp = (willow_strobe_parms_t *)data;

    if (wsp->period_ms >= MIN_STROBE_PERIOD) {
        period_ms = wsp->period_ms;
    }
    // this has the potential to leak if the task is deleted before we reach here
    free(wsp);

    ESP_LOGI(TAG, "starting display backlight strobe effect with period '%d'", period_ms);

    while (true) {
        display_set_backlight(true, true);
        vTaskDelay(period_ms / portTICK_PERIOD_MS);
        display_set_backlight(false, false);
        vTaskDelay(period_ms / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}
