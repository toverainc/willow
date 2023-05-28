# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(COMPONENTS main)

if(DEFINED ENV{WILLOW_VERSION})
	add_definitions(-DWILLOW_USER_AGENT="Willow/$ENV{WILLOW_VERSION}")
else()
	add_definitions(-DWILLOW_USER_AGENT="Willow/0.1")
endif()

include($ENV{ADF_PATH}/CMakeLists.txt)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(willow)
