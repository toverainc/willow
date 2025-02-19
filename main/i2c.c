#include "i2c.h"

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
