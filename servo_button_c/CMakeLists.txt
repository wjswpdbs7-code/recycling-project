# Pico 2 W 서보+버튼 테스트 (C)
cmake_minimum_required(VERSION 3.13)

# 보드 지정: Pico 2 W (RP2350 + 무선칩). 반드시 pico_sdk_import 전에!
set(PICO_BOARD pico2_w)

# Pico SDK 불러오기 (PICO_SDK_PATH 환경변수 사용)
include(pico_sdk_import.cmake)

project(servo_button C CXX ASM)
pico_sdk_init()

add_executable(servo_button main.c)

# pico_stdlib: GPIO/sleep/printf 기본, hardware_pwm: PWM 제어
target_link_libraries(servo_button pico_stdlib hardware_pwm)

# printf 출력을 USB 시리얼로 (UART는 끔 — GP0/GP1을 서보에 쓰기 때문)
pico_enable_stdio_usb(servo_button 1)
pico_enable_stdio_uart(servo_button 0)

# .uf2 파일 생성 (보드에 드래그&드롭으로 굽는 파일)
pico_add_extra_outputs(servo_button)
