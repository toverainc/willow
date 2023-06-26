#include "esp_lvgl_port.h"
#include "esp_peripherals.h"

#define DEFAULT_COMMAND_ENDPOINT "Home Assistant"
#define DEFAULT_MIC_GAIN          14
#define DEFAULT_SPEECH_REC_MODE  "WIS"

esp_lcd_panel_handle_t hdl_lcd;
esp_periph_set_handle_t hdl_pset;
