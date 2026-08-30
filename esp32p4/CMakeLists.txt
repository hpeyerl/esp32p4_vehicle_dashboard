cmake_minimum_required(VERSION 3.16.0)
file(WRITE "${CMAKE_SOURCE_DIR}/managed_components/lvgl__lvgl/lv_conf.h"
    "#include \"../../src/lv_conf.h\"\n")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(evj55-dashboard)
