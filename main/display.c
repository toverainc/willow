#include "board.h"
#include "esp_log.h"

#include "config.h"
#include "display.h"
#include "system.h"

#include "bsp/esp-bsp.h"
#include "slvgl.h"

enum willow_hw_t hw_type;

esp_err_t init_display(void)
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG()
    };
    cfg.lvgl_port_cfg.task_affinity = 1;
    ld = bsp_display_start_with_config(&cfg);

    bsp_display_backlight_on();

    return ESP_OK;
}

void display_set_backlight(const bool on, const bool max)
{
    if(on){
        bsp_display_backlight_on();
    } else {
        bsp_display_backlight_off();
    }
}
