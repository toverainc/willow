set(COMPONENT_SRCS "lv_img_hand_left.c" "tasks.c" "timer.c" "main.c")
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

set(MULTINET_CMDS_IN "${PROJECT_DIR}/speech_commands/commands_en.txt")
set(MULTINET_CMDS_OUT "${COMPONENT_DIR}/generated_cmd_multinet.h")

add_custom_command(
	OUTPUT ${MULTINET_CMDS_OUT}
	COMMAND awk -f ${PROJECT_DIR}/multinet.awk ${MULTINET_CMDS_IN} > ${MULTINET_CMDS_OUT}
	DEPENDS ${MULTINET_CMDS_IN}
	COMMENT "generating ${MULTINET_CMDS_OUT}"
)

add_custom_target(gen_mn DEPENDS "${MULTINET_CMDS_OUT}")
add_dependencies("${COMPONENT_LIB}" gen_mn)
set_property(DIRECTORY "${COMPONENT_DIR}" APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES "${MULTINET_CMDS_OUT}")
