# 투입구 양문 테스트
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

project(pico_door_test C CXX ASM)
pico_sdk_init()

add_executable(pico_door_test main.c)
target_link_libraries(pico_door_test pico_stdlib hardware_pwm)

pico_enable_stdio_usb(pico_door_test 1)
pico_enable_stdio_uart(pico_door_test 0)

pico_add_extra_outputs(pico_door_test)
