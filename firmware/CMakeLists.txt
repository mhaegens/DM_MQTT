cmake_minimum_required(VERSION 3.16)

# Include Waveshare BSP as a component (download from Waveshare wiki and place in
# firmware/components/waveshare_bsp/).
# The BSP provides display init (MIPI-DSI), GT911 touch, and the LVGL port for P4.
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/components"
    "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sapdm_simulator)
