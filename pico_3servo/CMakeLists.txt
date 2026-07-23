# 서보 3개 테스트 — 투입구 양문 + 셔터
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

project(pico_3servo C CXX ASM)
pico_sdk_init()

add_executable(pico_3servo main.c)
target_link_libraries(pico_3servo pico_stdlib hardware_pwm)

pico_enable_stdio_usb(pico_3servo 1)
pico_enable_stdio_uart(pico_3servo 0)

pico_add_extra_outputs(pico_3servo)
