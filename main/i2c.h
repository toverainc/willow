#include "driver/i2c.h"

void init_i2c(void);
esp_err_t i2c_probe(i2c_port_t port, uint8_t addr);
