esp_timer_handle_t hdl_sess_timer;
xQueueHandle hdl_q_timer;

esp_err_t init_session_timer(void);
void init_timer(void);
void reset_timer(bool pause);