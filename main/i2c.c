#include "board.h"
#include "esp_log.h"
#include "i2c_bus.h"

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

esp_err_t i2c_probe(i2c_port_t port, uint8_t addr)
{
    esp_err_t ret = ESP_ERR_NOT_FOUND;

    i2c_cmd_handle_t hdl_i2c_cmd = i2c_cmd_link_create();
    i2c_master_start(hdl_i2c_cmd);
    i2c_master_write_byte(hdl_i2c_cmd, (addr << 1) | I2C_MASTER_WRITE, I2C_MASTER_ACK);
    i2c_master_stop(hdl_i2c_cmd);

    ret = i2c_master_cmd_begin(port, hdl_i2c_cmd, 5000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(hdl_i2c_cmd);

    return ret;
}
