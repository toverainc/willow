#include "esp_audio.h"
#include "esp_lvgl_port.h"
#include "esp_netif.h"
#include "esp_peripherals.h"

bool recording;

enum willow_state {
    STATE_INIT = 0,
    STATE_NVS_OK,
    STATE_CONFIG_OK,
    STATE_READY,
};

enum willow_state state;

esp_audio_handle_t hdl_ea;
esp_lcd_panel_handle_t hdl_lcd;
esp_periph_set_handle_t hdl_pset;

extern QueueHandle_t q_rec;

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
