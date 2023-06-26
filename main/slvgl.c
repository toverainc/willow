#include "board.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_tt21100.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "periph_lcd.h"

#include "audio.h"
#include "display.h"
#include "system.h"
#include "timer.h"

static const char *TAG = "WILLOW/LVGL";

// this is absolutely horrendous but lvgl_port_esp32 requires esp_lcd_panel_io_handle_t and esp-adf does not expose this
typedef struct periph_lcd {
    void *io_bus;
    get_lcd_io_bus new_panel_io;
    esp_lcd_panel_io_spi_config_t lcd_io_cfg;
    get_lcd_panel new_lcd_panel;
    esp_lcd_panel_dev_config_t lcd_dev_cfg;

    esp_lcd_panel_io_handle_t lcd_io_handle;
    esp_lcd_panel_handle_t lcd_panel_handle;

    perph_lcd_rest rest_cb;
    void *rest_cb_ctx;
    bool lcd_swap_xy;
    bool lcd_mirror_x;
    bool lcd_mirror_y;
    bool lcd_color_invert;
} periph_lcd_t;

esp_lcd_panel_handle_t hdl_lcd = NULL;
lv_disp_t *ld;
lv_obj_t *btn_cancel, *lbl_btn_cancel, *lbl_ln1, *lbl_ln2, *lbl_ln3, *lbl_ln4, *lbl_ln5;

static periph_lcd_t *lcdp;

void cb_btn_cancel(lv_event_t *ev)
{
    ESP_LOGD(TAG, "btn_cancel pressed");
    q_msg msg = MSG_STOP;
    xQueueSend(q_rec, &msg, 0);
}

void cb_scr(lv_event_t *ev)
{
    // printf("cb_scr\n");
    switch (lv_event_get_code(ev)) {
        case LV_EVENT_RELEASED:
            reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, false);
            break;

        case LV_EVENT_PRESSED:
            reset_timer(hdl_display_timer, DISPLAY_TIMEOUT_US, true);
            display_set_backlight(true);
            break;

        default:
            break;
    }
}

esp_err_t init_lvgl_display(void)
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
    lcdp = esp_periph_get_data(hdl_plcd);

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
            .buff_dma = true,
            .buff_spiram = false,
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

    return ret;
}

esp_err_t init_lvgl_touch(void)
{
    esp_err_t ret = ESP_OK;
    esp_lcd_touch_config_t cfg_lt = {
        .flags = {
            .mirror_x = true,
            .mirror_y = false,
            .swap_xy = LCD_SWAP_XY,
        },
        .levels = {
            .interrupt = 0,
            .reset = 0,
        },
        .int_gpio_num = GPIO_NUM_3,
        .rst_gpio_num = GPIO_NUM_NC,
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
    };

    esp_lcd_panel_io_i2c_config_t cfg_io_lt = ESP_LCD_TOUCH_IO_I2C_TT21100_CONFIG();
    ret = esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)0, &cfg_io_lt, &lcdp->lcd_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize display panel IO: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_lcd_touch_handle_t hdl_lt = NULL;
    ret = esp_lcd_touch_new_i2c_tt21100(lcdp->lcd_io_handle, &cfg_lt, &hdl_lt);
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
