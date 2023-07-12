#include "esp_lcd_types.h"

esp_lcd_panel_handle_t hdl_lcd;

esp_err_t init_display(void);
void display_set_backlight(const bool on);