extern esp_lcd_panel_handle_t hdl_lcd;
extern lv_disp_t *ld;
extern lv_obj_t *lbl_ln1, *lbl_ln2, *lbl_ln3, *lbl_ln4;

void cb_scr(lv_event_t *ev);
esp_err_t init_lvgl_display(void);
esp_err_t init_lvgl_touch(void);