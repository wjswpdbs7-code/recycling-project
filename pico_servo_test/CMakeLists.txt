# 서보 전수 테스트 (GP0/GP1/GP2)
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

project(pico_servo_test C CXX ASM)
pico_sdk_init()

add_executable(pico_servo_test main.c)
target_link_libraries(pico_servo_test pico_stdlib hardware_pwm)

pico_enable_stdio_usb(pico_servo_test 1)
pico_enable_stdio_uart(pico_servo_test 0)

pico_add_extra_outputs(pico_servo_test)
