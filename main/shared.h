#include "esp_peripherals.h"

#define WAKENET_NAME "wn9_alexa"

esp_periph_set_handle_t hdl_pset;
lv_obj_t *lbl_ln1, *lbl_ln2, *lbl_ln3, *lbl_ln4;

static const char *TAG = "SALLOW";