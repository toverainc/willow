#include "esp_lvgl_port.h"
#include "esp_netif.h"
#include "esp_peripherals.h"

enum willow_state {
    STATE_INIT = 0,
    STATE_NVS_OK,
    STATE_CONFIG_OK,
    STATE_READY,
};

enum willow_state state;

esp_lcd_panel_handle_t hdl_lcd;
esp_periph_set_handle_t hdl_pset;