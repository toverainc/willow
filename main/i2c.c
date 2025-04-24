#include "board.h"
#include "esp_log.h"

#include "i2c.h"

static const char *TAG = "WILLOW/I2C";

i2c_bus_handle_t hdl_i2c_bus;

void init_i2c(void)
{
    int ret = ESP_OK;
    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ret = get_i2c_pins(I2C_NUM_0, &i2c_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to get I2C pins");
    }
    hdl_i2c_bus = i2c_bus_create(I2C_NUM_0, &i2c_cfg);
}
