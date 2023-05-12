#include "esp_peripherals.h"

#define WAKENET_NAME "wn9_alexa"

esp_periph_set_handle_t hdl_pset;

extern QueueHandle_t q_rec;

static const char *TAG = "SALLOW";

typedef enum {
    MSG_STOP,
    MSG_START,
    MSG_START_LOCAL,
} q_msg;