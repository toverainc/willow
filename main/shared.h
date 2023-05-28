#include "esp_audio.h"
#include "esp_peripherals.h"

bool recording;

esp_audio_handle_t hdl_ea;
esp_periph_set_handle_t hdl_pset;

extern QueueHandle_t q_rec;

static const char *TAG = "WILLOW";

typedef enum {
    MSG_STOP,
    MSG_START,
    MSG_START_LOCAL,
} q_msg;

void play_tone_err(void *data);
void play_tone_ok(void *data);