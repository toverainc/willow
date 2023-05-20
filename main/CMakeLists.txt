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

if(CONFIG_WILLOW_USE_ENDPOINT_HOMEASSISTANT)
	set(COMPONENT_SRCS ${COMPONENT_SRCS} "endpoint/hass.c")
endif()

register_component()

spiffs_create_partition_image(audio ../spiffs/audio FLASH_IN_PROJECT)
