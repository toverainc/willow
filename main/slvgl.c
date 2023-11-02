#include "board.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_touch_tt21100.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "i2c_bus.h"
#include "lvgl.h"
#include "periph_lcd.h"

#include "audio.h"
#include "config.h"
#include "display.h"
#include "system.h"
#include "timer.h"

#define DEFAULT_LOCK_TIMEOUT 500

static const char *TAG = "WILLOW/LVGL";

int lvgl_lock_timeout;
lv_disp_t *ld;
lv_obj_t *btn_cancel, *lbl_btn_cancel, *lbl_ln1, *lbl_ln2, *lbl_ln3, *lbl_ln4, *lbl_ln5;

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
            reset_timer(hdl_display_timer, config_get_int("display_timeout", DEFAULT_DISPLAY_TIMEOUT), false);
            break;

        case LV_EVENT_PRESSED:
            reset_timer(hdl_display_timer, config_get_int("display_timeout", DEFAULT_DISPLAY_TIMEOUT), true);
            display_set_backlight(true, false);
            break;

        default:
            break;
    }
}