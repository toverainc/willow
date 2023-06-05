#include "board.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "shared.h"

#define WIS_URL_TTS_ARG "?speaker=CLB&text=%s"
#define WIS_URL_TTS_FMT CONFIG_WILLOW_WIS_TTS_URL WIS_URL_TTS_ARG

#if defined(CONFIG_WILLOW_AUDIO_RESPONSE_FS)
static void play_audio_err(void *data)
{
    gpio_set_level(get_pa_enable_gpio(), 1);
    esp_audio_sync_play(hdl_ea, "spiffs://spiffs/ui/error.flac", 0);
    gpio_set_level(get_pa_enable_gpio(), 0);
}

static void play_audio_ok(void *data)
{
    gpio_set_level(get_pa_enable_gpio(), 1);
    esp_audio_sync_play(hdl_ea, "spiffs://spiffs/ui/success.flac", 0);
    gpio_set_level(get_pa_enable_gpio(), 0);
}

#elif defined(CONFIG_WILLOW_AUDIO_RESPONSE_WIS_TTS)
static void play_audio_wis_tts(void *data)
{
    if (data == NULL) {
        ESP_LOGW(TAG, "called play_audio_wis_tts with NULL data");
        return;
    }
    int len_url = strlen(WIS_URL_TTS_FMT) + strlen((char *)data) + 1;
    char *url = calloc(sizeof(char), len_url);
    snprintf(url, len_url, WIS_URL_TTS_FMT, (char *)data);
    gpio_set_level(get_pa_enable_gpio(), 1);
    ESP_LOGI(TAG, "Using WIS TTS URL '%s'", url);
    esp_audio_sync_play(hdl_ea, url, 0);
    ESP_LOGI(TAG, "WIS TTS playback finished");
    gpio_set_level(get_pa_enable_gpio(), 0);
    free(url);
}
#else

static void noop(void)
{
}
#endif

void init_audio_response(void)
{
#if defined(CONFIG_WILLOW_AUDIO_RESPONSE_FS)
    war.fn_err = play_audio_err;
    war.fn_ok = play_audio_ok;
#elif defined(CONFIG_WILLOW_AUDIO_RESPONSE_WIS_TTS)
    war.fn_err = play_audio_wis_tts;
    war.fn_ok = play_audio_wis_tts;
#else
    war.fn_err = noop;
    war.fn_ok = noop;
#endif
}