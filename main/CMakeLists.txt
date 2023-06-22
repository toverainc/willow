set(COMPONENT_SRCS
    "audio.c"
    "config.c"
    "display.c"
    "endpoint/hass.c"
    "endpoint/openhab.c"
    "endpoint/rest.c"
    "http.c"
    "input.c"
    "log.c"
    "lv_img_hand_left.c"
    "network.c"
    "ota.c"
    "slvgl.c"
    "system.c"
    "tasks.c"
    "timer.c"
    "ui.c"
    "was.c"
    "main.c"
)
set(COMPONENT_ADD_INCLUDEDIRS .)
set(COMPONENT_PRIV_REQUIRES
    app_update
    audio_board
    audio_hal
    audio_pipeline
    audio_recorder
    audio_sal
    audio_stream
    esp_http_client
    esp_peripherals
    esp_websocket_client
    input_key_service
    spiffs
)

if(CONFIG_WILLOW_ETHERNET)
    set(COMPONENT_SRCS ${COMPONENT_SRCS} "net/ethernet.c")
endif()

register_component()

spiffs_create_partition_image(user ../spiffs/user FLASH_IN_PROJECT)

# sdkconfig sanity checks
if($ENV{WILLOW_SDKCONFIG_SANITY_CHECKS})
    if (NOT CONFIG_WILLOW_ETHERNET)
        string(REGEX MATCH "^[ -~]+" WIFI_PASSPHRASE "${CONFIG_WIFI_PASSWORD}")
        if(NOT "${CONFIG_WIFI_PASSWORD}" STREQUAL "${WIFI_PASSPHRASE}")
            message(FATAL_ERROR "Wi-Fi WPA passphrase must only contain ASCII characters")
        endif()

        string(LENGTH "${CONFIG_WIFI_PASSWORD}" WIFI_PLEN)
        if(WIFI_PLEN LESS 8 OR WIFI_PLEN GREATER 63)
            message(FATAL_ERROR "Wi-Fi WPA passphrase must be between 8 and 63 ASCII characters")
        endif()
    endif()

endif()
