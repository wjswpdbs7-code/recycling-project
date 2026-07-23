# Pico 2 W HC-SR04 초음파 테스트 (C)
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

project(hcsr04_test C CXX ASM)
pico_sdk_init()

add_executable(hcsr04_test main.c)

target_link_libraries(hcsr04_test pico_stdlib)

pico_enable_stdio_usb(hcsr04_test 1)
pico_enable_stdio_uart(hcsr04_test 0)

pico_add_extra_outputs(hcsr04_test)

# 배선 진단용 펌웨어
add_executable(hcsr04_diag diag.c)
target_link_libraries(hcsr04_diag pico_stdlib)
pico_enable_stdio_usb(hcsr04_diag 1)
pico_enable_stdio_uart(hcsr04_diag 0)
pico_add_extra_outputs(hcsr04_diag)
