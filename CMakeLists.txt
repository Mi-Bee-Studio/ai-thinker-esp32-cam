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
file(GLOB MIBEE_UI_FILES main/web_ui/*)
# 2026-09-03：目录级依赖不随文件内容编辑触发（三姐妹仓同坑已修），必须显式 DEPENDS
# 2026-09-09 PIT-043：压缩进构建。compress_ui.py 原为手工步骤——改 SPA 源文件后忘跑，
# 旧 .gz（gitignored、family_check 不可见）随镜像进固件，设备静默服务旧 UI
# （实例：备用 WiFi 字段不显示）。gz 与源文件同 GLOB 入 DEPENDS，Ninja 保序。
file(GLOB MIBEE_UI_SRC main/web_ui/*.js main/web_ui/*.html main/web_ui/*.css main/web_ui/*.svg)
# 2026-09-13 修正：OUTPUT 必须显式进 spiffs 的 DEPENDS——file(GLOB) 在干净检出上
# 扫不到尚不存在的 .gz，custom command 成孤儿、压缩静默跳过（新机/CI 必现；
# 老机靠历史手动 gz 维持假象）。显式列出 5 个 gz 路径，缺谁 Ninja 就向
# custom command 要谁。
set(MIBEE_UI_GZ ${CMAKE_SOURCE_DIR}/main/web_ui/app.js.gz
                ${CMAKE_SOURCE_DIR}/main/web_ui/index.html.gz
                ${CMAKE_SOURCE_DIR}/main/web_ui/style.css.gz
                ${CMAKE_SOURCE_DIR}/main/web_ui/i18n.js.gz
                ${CMAKE_SOURCE_DIR}/main/web_ui/favicon.svg.gz)
add_custom_command(
    OUTPUT ${MIBEE_UI_GZ}
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/compress_ui.py
    DEPENDS ${MIBEE_UI_SRC} ${CMAKE_SOURCE_DIR}/tools/compress_ui.py
    COMMENT "gzip web UI (auto, PIT-043)"
    VERBATIM)
spiffs_create_partition_image(spiffs main/web_ui FLASH_IN_PROJECT DEPENDS ${MIBEE_UI_FILES} ${MIBEE_UI_GZ})
