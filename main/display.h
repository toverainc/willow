#include "periph_lcd.h"

extern esp_lcd_panel_handle_t hdl_lcd;

esp_err_t init_display_handle(void)
esp_err_t init_display_bl(void);
void display_set_backlight(const bool on, const bool max);
