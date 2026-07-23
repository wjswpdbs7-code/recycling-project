# 전체 HW 테스트: FSR 8채널 + 서보3 + LCD
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

project(pico_fsr_test C CXX ASM)
pico_sdk_init()

add_executable(pico_fsr_test main.c)
target_link_libraries(pico_fsr_test pico_stdlib hardware_pwm hardware_i2c)

pico_enable_stdio_usb(pico_fsr_test 1)
pico_enable_stdio_uart(pico_fsr_test 0)

pico_add_extra_outputs(pico_fsr_test)
