#include "esp_audio.h"

struct willow_audio_response {
    void (*fn_err)(void *data);
    void (*fn_ok)(void *data);
};

typedef enum {
    MSG_STOP,
    MSG_START,
    MSG_START_LOCAL,
} q_msg;

bool recording;
esp_audio_handle_t hdl_ea;
extern QueueHandle_t q_rec;
struct willow_audio_response war;

void deinit_audio(void);
void init_audio(void);