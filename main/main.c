#include "driver/i2c.h"
#include "board.h"
#include "es7210.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "periph_spiffs.h"
#include "sdkconfig.h"

#include "audio.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "log.h"
#include "network.h"
#include "shared.h"
#include "slvgl.h"
#include "system.h"
#include "tasks.h"
#include "timer.h"
#include "ui.h"
#include "was.h"

#include "endpoint/hass.h"

#if defined(CONFIG_WILLOW_ETHERNET)
#include "net/ethernet.h"
#endif

#define DEFAULT_WIS_URL "https://infer.tovera.io/api/willow"

#define I2S_PORT       I2S_NUM_0
#define PARTLABEL_USER "user"

char was_url[2048];
static const char *TAG = "WILLOW/MAIN";
enum willow_state state;

esp_periph_set_handle_t hdl_pset;

static esp_err_t init_spiffs_user(void)
{
    esp_err_t ret = ESP_OK;
    periph_spiffs_cfg_t pcfg_spiffs_user = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .partition_label = PARTLABEL_USER,
        .root = "/spiffs/user",
    };
    esp_periph_handle_t phdl_spiffs_user = periph_spiffs_init(&pcfg_spiffs_user);
    ret = esp_periph_start(hdl_pset, phdl_spiffs_user);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to start spiffs user peripheral: %s", esp_err_to_name(ret));
        return ret;
    }

    while (!periph_spiffs_is_mounted(phdl_spiffs_user)) {
        ESP_LOGI(TAG, "Waiting on SPIFFS mount...");
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "SPIFFS mounted");

    return ret;
}

#define ACK_CHECK_EN 1
static i2c_port_t i2c_port = I2C_NUM_0;

#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define WRITE_BIT I2C_MASTER_WRITE  /*!< I2C master write */
static int do_i2cdetect_cmd()
{
    // i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, ESP_INTR_FLAG_IRAM);
    // i2c_master_driver_initialize();
    uint8_t address;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (address << 1) | WRITE_BIT, ACK_CHECK_EN);
            i2c_master_stop(cmd);
            esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 50 / portTICK_PERIOD_MS);
            i2c_cmd_link_delete(cmd);
            if (ret == ESP_OK) {
                printf("%02x ", address);
            } else if (ret == ESP_ERR_TIMEOUT) {
                printf("UU ");
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }

    // i2c_driver_delete(i2c_port);
    return 0;
}

void app_main(void)
{
    state = STATE_INIT;
    esp_err_t ret;

    init_logging();
    ESP_LOGI(TAG, "Starting up! Please wait...");

    esp_periph_config_t pcfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    hdl_pset = esp_periph_set_init(&pcfg);

    init_system();
    init_spiffs_user();
    config_parse();
    init_display();
    init_esp_lvgl_port();
    init_lvgl_display();
    init_ui();

    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

#ifdef CONFIG_WILLOW_ETHERNET
    init_ethernet();
#else
    nvs_handle_t hdl_nvs;
    ret = nvs_open("WIFI", NVS_READONLY, &hdl_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to open NVS namespace WIFI: %s", esp_err_to_name(err));
        goto err_nvs;
    }

    char psk[64];
    size_t sz = sizeof(psk);
    ret = nvs_get_str(hdl_nvs, "PSK", psk, &sz);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to get PSK from NVS namespace WIFI: %s", esp_err_to_name(err));
        goto err_nvs;
    }

    char ssid[33];
    sz = sizeof(ssid);
    ret = nvs_get_str(hdl_nvs, "SSID", ssid, &sz);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to get PSK from NVS namespace WIFI: %s", esp_err_to_name(err));
        goto err_nvs;
    }
    init_wifi(psk, ssid);
#endif

    ret = nvs_open("WAS", NVS_READONLY, &hdl_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to open NVS namespace WAS: %s", esp_err_to_name(err));
        goto err_nvs;
    }
    sz = sizeof(was_url);
    ret = nvs_get_str(hdl_nvs, "URL", was_url, &sz);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to get WASL URL from NVS namespace WAS: %s", esp_err_to_name(err));
        goto err_nvs;
    }
    state = STATE_NVS_OK;
    ret = init_was();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize Willow Application Server connection");
        ui_pr_err("Fatal error!", "WAS initialization failed.");
    }

    if (!config_valid) {
        // wait "indefinitely"
        vTaskDelay(portMAX_DELAY);
    }

// we jump over WAS initialization was without Wi-Fi this will never work
err_nvs:
    if (state < STATE_NVS_OK) {
        ui_pr_err("Fatal error!", "Failed to read NVS partition.");
        // wait "indefinitely"
        vTaskDelay(portMAX_DELAY);
    }

    bool was_mode = config_get_bool("was_mode", DEFAULT_WAS_MODE);
    if (!was_mode) {
        char *command_endpoint = config_get_char("command_endpoint", DEFAULT_COMMAND_ENDPOINT);
        if (strcmp(command_endpoint, "Home Assistant") == 0) {
            init_hass();
        }
        free(command_endpoint);
    }

#if 0
    init_buttons();
    init_input_key_service();
#endif
    init_audio();
    do_i2cdetect_cmd();

    init_lvgl_touch();
    init_display_timer();

#ifndef CONFIG_WILLOW_ETHERNET
    get_mac_address(); // should be on wifi by now; print the MAC
#endif

    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "Startup complete! Hardware: %s. Version: %s. Waiting for wake word.", str_hw_type(hw_type),
             app_desc->version);

    // if we reached this point, we can mark the current partition valid
    // we can still crash on wake or other events but we should be able to do another OTA
    // we can also still crash in the while loop below - this should be improved
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_mark_app_valid_cancel_rollback());

    ESP_ERROR_CHECK_WITHOUT_ABORT(
        reset_timer(hdl_display_timer, config_get_int("display_timeout", DEFAULT_DISPLAY_TIMEOUT), false));

#ifdef CONFIG_WILLOW_DEBUG_RUNTIME_STATS
    xTaskCreate(&task_debug_runtime_stats, "dbg_runtime_stats", 4 * 1024, NULL, 0, NULL);
#endif

    while (true) {
#ifdef CONFIG_WILLOW_DEBUG_MEM
        printf("MALLOC_CAP_INTERNAL:\n");
        heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
        printf("MALLOC_CAP_SPIRAM:\n");
        heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
#endif
#ifdef CONFIG_WILLOW_DEBUG_TASKS
        char buf[128];
        vTaskList(buf);
        printf("%s\n", buf);
#endif
#ifdef CONFIG_WILLOW_DEBUG_TIMERS
        (esp_timer_dump(stdout));
#endif
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
