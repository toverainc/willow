set(COMPONENT_SRCS "lv_img_hand_left.c" "tasks.c" "main.c")
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
	spiffs
)
register_component()

spiffs_create_partition_image(audio ../spiffs/audio FLASH_IN_PROJECT)
