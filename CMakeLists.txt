cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mibee_cam)

# Derive version from git tags: v1.2.3-4-gabcdef → "v1.2.3+4.gabcdef"
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_VERSION)
    set(GIT_VERSION "unknown")
endif()
set(PROJECT_VER "${GIT_VERSION}")

# Embed web UI files into SPIFFS partition
spiffs_create_partition_image(spiffs main/web_ui FLASH_IN_PROJECT)
