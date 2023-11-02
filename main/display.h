#include "periph_lcd.h"

extern esp_lcd_panel_handle_t lcd_panel;
extern esp_lcd_panel_io_handle_t lcd_io;

esp_err_t init_display(void);
void display_set_backlight(const bool on, const bool max);
