idf_component_register(COMPONENT_SRCS
    SRC_DIRS
    "."
    "./endpoint"
    INCLUDE_DIRS
    ".")

if(CONFIG_WILLOW_ETHERNET)
    set(COMPONENT_SRCS ${COMPONENT_SRCS} "net/ethernet.c")
endif()

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
