cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# The Arduino-based Avatar stays in the tree but out of the build
# until its Arduino dependencies are ported to ESP-IDF.
set(EXCLUDE_COMPONENTS m5stack_avatar)
project(stackchan_usb_hub)
