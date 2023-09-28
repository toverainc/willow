extern char was_url[2048];

void deinit_was(void);
esp_err_t init_was(void);
void request_config(void);
void send_wake_start(float wake_volume);
void send_wake_end(void);
