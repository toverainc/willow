extern lv_disp_t *ld;
extern lv_obj_t *btn_cancel, *lbl_btn_cancel, *lbl_ln1, *lbl_ln2, *lbl_ln3, *lbl_ln4, *lbl_ln5;

void cb_btn_cancel(lv_event_t *);
void cb_scr(lv_event_t *ev);
esp_err_t init_lvgl_display(void);
esp_err_t init_lvgl_touch(void);