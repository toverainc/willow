set(COMPONENT_SRCS "i2s.c" "tasks.c" "test.c")
set(COMPONENT_ADD_INCLUDEDIRS .)

register_component()

spiffs_create_partition_image(audio ../spiffs/audio FLASH_IN_PROJECT)
