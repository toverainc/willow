set(COMPONENT_SRCS
	"http.c"
	"lv_img_hand_left.c"
	"slvgl.c"
	"tasks.c"
	"timer.c"
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

if(CONFIG_HOMEASSISTANT_HOST MATCHES ^https?://)
	message(FATAL_ERROR "Home Assistant host must not contain URL scheme")
endif()

if(CONFIG_WILLOW_USE_ENDPOINT_HOMEASSISTANT)
	set(COMPONENT_SRCS ${COMPONENT_SRCS} "endpoint/hass.c")
elseif(CONFIG_WILLOW_USE_ENDPOINT_OPENHAB)
	set(COMPONENT_SRCS ${COMPONENT_SRCS} "endpoint/openhab.c")
elseif(CONFIG_WILLOW_USE_ENDPOINT_REST)
	set(COMPONENT_SRCS "${COMPONENT_SRCS}" "endpoint/rest.c")
endif()

register_component()

spiffs_create_partition_image(audio ../spiffs/audio FLASH_IN_PROJECT)

string(REGEX MATCH "^[ -~]+" WIFI_PASSPHRASE "${CONFIG_WIFI_PASSWORD}")
if(NOT "${CONFIG_WIFI_PASSWORD}" STREQUAL "${WIFI_PASSPHRASE}")
	message(FATAL_ERROR "Wi-Fi WPA passphrase must only contain ASCII characters")
endif()

string(LENGTH "${CONFIG_WIFI_PASSWORD}" WIFI_PLEN)
if(WIFI_PLEN LESS 8 OR WIFI_PLEN GREATER 63)
	message(FATAL_ERROR "Wi-Fi WPA passphrase must be between 8 and 63 ASCII characters")
endif()
