#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "config.h"
#include "shared.h"
#include "slvgl.h"

static const char *TAG = "WILLOW/UI";

void init_ui(void)
{
    if (ld == NULL) {
        ESP_LOGE(TAG, "lv_disp_t ld is NULL!!!!");
    } else {

        lvgl_port_lock(0);

        lv_obj_t *scr_act = lv_disp_get_scr_act(ld);
        lv_obj_t *lbl_hdr = lv_label_create(scr_act);
        btn_cancel = lv_btn_create(scr_act);
        lbl_btn_cancel = lv_label_create(btn_cancel);
        lbl_ln1 = lv_label_create(scr_act);
        lbl_ln2 = lv_label_create(scr_act);
        lbl_ln3 = lv_label_create(scr_act);
        lbl_ln4 = lv_label_create(scr_act);
        lbl_ln5 = lv_label_create(scr_act);
        lv_obj_set_style_text_align(lbl_ln3, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_recolor(lbl_ln4, true);
        lv_label_set_recolor(lbl_ln5, true);
        lv_obj_add_event_cb(scr_act, cb_scr, LV_EVENT_ALL, NULL);

#ifdef CONFIG_LV_USE_FS_POSIX
        // Willow style
        static lv_style_t lv_st_willow;
        lv_style_init(&lv_st_willow);

        // Willow font
        lv_font_t *lv_font_willow;
        lv_font_willow = lv_font_load("A/spiffs/user/font/tonnelier.bin");

        // Attach font to style
        lv_style_set_text_font(&lv_st_willow, lv_font_willow);

        // Set Willow style on objects
        lv_obj_add_style(lbl_hdr, &lv_st_willow, 0);
        lv_obj_add_style(lbl_ln1, &lv_st_willow, 0);
        lv_obj_add_style(lbl_ln2, &lv_st_willow, 0);
        lv_obj_add_style(lbl_ln3, &lv_st_willow, 0);
        lv_obj_add_style(lbl_ln4, &lv_st_willow, 0);
        lv_obj_add_style(lbl_ln5, &lv_st_willow, 0);
        lv_obj_add_style(lbl_btn_cancel, &lv_st_willow, 0);
#endif

        lv_label_set_text_static(lbl_btn_cancel, "Cancel");
        lv_label_set_text_static(lbl_hdr, "Welcome to Willow!");
        lv_obj_add_flag(btn_cancel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ln1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ln2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ln5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_align(lbl_btn_cancel, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(lbl_hdr, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_align(lbl_ln1, LV_ALIGN_TOP_LEFT, 0, 30);
        lv_obj_align(lbl_ln2, LV_ALIGN_TOP_LEFT, 0, 60);
        lv_obj_align(lbl_ln3, LV_ALIGN_TOP_MID, 0, 90);
        lv_obj_align(lbl_ln4, LV_ALIGN_TOP_MID, 0, 120);
        lv_obj_align(lbl_ln5, LV_ALIGN_TOP_LEFT, 0, 150);
        lv_label_set_long_mode(lbl_ln2, LV_LABEL_LONG_SCROLL);
        lv_obj_set_width(lbl_ln2, 320);

        if (strcmp(config_get_char("speech_rec_mode"), "Multinet") == 0) {
            lv_label_set_text_static(lbl_ln3, "Starting up (local)...");
        } else if (strcmp(config_get_char("speech_rec_mode"), "WIS") == 0) {
            lv_label_set_text_static(lbl_ln3, "Starting up (server)...");
        }
        lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);

        lvgl_port_unlock();
    }
}
