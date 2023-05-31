#include "esp_audio.h"
#include "esp_peripherals.h"

#define DISPLAY_TIMEOUT_US 10 * 1000 * 1000

bool recording;

esp_audio_handle_t hdl_ea;
esp_periph_set_handle_t hdl_pset;
esp_timer_handle_t hdl_display_timer, hdl_sess_timer;

extern QueueHandle_t q_rec;

static const char *TAG = "WILLOW";

struct willow_audio_response {
    void (*fn_err)(void *data);
    void (*fn_ok)(void *data);
};

struct willow_audio_response war;

typedef enum {
    MSG_STOP,
    MSG_START,
    MSG_START_LOCAL,
} q_msg;
