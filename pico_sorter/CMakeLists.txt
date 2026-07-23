# 분리수거 자동분류기 구동부 (Pico 2 W)
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

project(pico_sorter C CXX ASM)
pico_sdk_init()

add_executable(pico_sorter main.c)

target_link_libraries(pico_sorter pico_stdlib hardware_pwm hardware_i2c)

pico_enable_stdio_usb(pico_sorter 1)
pico_enable_stdio_uart(pico_sorter 0)

pico_add_extra_outputs(pico_sorter)
