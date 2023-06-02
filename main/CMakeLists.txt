set(COMPONENT_SRCS
    "display.c"
    "http.c"
    "input.c"
    "lv_img_hand_left.c"
    "network.c"
    "slvgl.c"
    "tasks.c"
    "timer.c"
    "ui.c"
    "main.c"
)
set(COMPONENT_ADD_INCLUDEDIRS .)
set(COMPONENT_PRIV_REQUIRES
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

if(CONFIG_WILLOW_USE_ENDPOINT_HOMEASSISTANT)
    set(COMPONENT_SRCS ${COMPONENT_SRCS} "endpoint/hass.c")
elseif(CONFIG_WILLOW_USE_ENDPOINT_OPENHAB)
    set(COMPONENT_SRCS ${COMPONENT_SRCS} "endpoint/openhab.c")
elseif(CONFIG_WILLOW_USE_ENDPOINT_REST)
    set(COMPONENT_SRCS "${COMPONENT_SRCS}" "endpoint/rest.c")
endif()

register_component()

spiffs_create_partition_image(ui ../spiffs/ui FLASH_IN_PROJECT)

# sdkconfig sanity checks
if($ENV{WILLOW_SDKCONFIG_SANITY_CHECKS})
    if(CONFIG_WILLOW_USE_WIS AND NOT CONFIG_SERVER_URI MATCHES "^https?://")
        message(FATAL_ERROR "Willow WIS URL must include URL scheme")
    endif()

    string(REGEX MATCH "^[ -~]+" WIFI_PASSPHRASE "${CONFIG_WIFI_PASSWORD}")
    if(NOT "${CONFIG_WIFI_PASSWORD}" STREQUAL "${WIFI_PASSPHRASE}")
        message(FATAL_ERROR "Wi-Fi WPA passphrase must only contain ASCII characters")
    endif()

    string(LENGTH "${CONFIG_WIFI_PASSWORD}" WIFI_PLEN)
    if(WIFI_PLEN LESS 8 OR WIFI_PLEN GREATER 63)
        message(FATAL_ERROR "Wi-Fi WPA passphrase must be between 8 and 63 ASCII characters")
    endif()

    if(CONFIG_WILLOW_USE_ENDPOINT_HOMEASSISTANT)
        if(CONFIG_HOMEASSISTANT_HOST MATCHES ^https?://)
            message(FATAL_ERROR "Home Assistant host must not contain URL scheme")
        endif()

        string(LENGTH "${CONFIG_HOMEASSISTANT_TOKEN}" HASS_TLEN)
        if(HASS_TLEN EQUAL 0)
            message(FATAL_ERROR "Home Assistant endpoint configured without token")
        endif()
    endif()

    if(CONFIG_WILLOW_USE_ENDPOINT_OPENHAB)
        string(LENGTH "${CONFIG_WILLOW_ENDPOINT_OPENHAB_TOKEN}" OH_TLEN)
        if(OH_TLEN EQUAL 0)
            message(FATAL_ERROR "openHAB endpoint configured without token")
        endif()

        if(NOT CONFIG_WILLOW_ENDPOINT_OPENHAB_URL MATCHES "^https?://")
            message(FATAL_ERROR "openHAB endpoint URL must contain URL scheme")
        endif()
    endif()

    if(CONFIG_WILLOW_USE_ENDPOINT_REST)
        if(CONFIG_WILLOW_ENDPOINT_REST_AUTH_AUTH_HEADER)
            string(LENGTH "${CONFIG_WILLOW_ENDPOINT_REST_AUTH_HEADER}" RA_HLEN)
            if(RA_HLEN EQUAL 0)
                message(FATAL_ERROR "REST endpoint configured to use Authorization header with empty header")
            endif()
        endif()

        if(CONFIG_WILLOW_ENDPOINT_REST_AUTH_BASIC)
            string(LENGTH "${CONFIG_WILLOW_ENDPOINT_REST_AUTH_PASSWORD}" RA_PLEN)
            string(LENGTH "${CONFIG_WILLOW_ENDPOINT_REST_AUTH_USERNAME}" RA_ULEN)
            if(RA_PLEN EQUAL 0 AND RA_ULEN EQUAL 0)
                message(FATAL_ERROR "REST endpoint configured to use Basic Authentication with empty username and password")
            endif()
        endif()

        if(NOT CONFIG_WILLOW_ENDPOINT_REST_URL MATCHES "^https?://")
            message(FATAL_ERROR "REST endpoint URL must contain URL scheme")
        endif()
    endif()

    if(CONFIG_WILLOW_AUDIO_RESPONSE_WIS_TTS AND NOT CONFIG_WILLOW_WIS_TTS_URL MATCHES "^https?://")
        message(FATAL_ERROR "Willow WIS TTS URL must include URL scheme")
    endif()
endif()
