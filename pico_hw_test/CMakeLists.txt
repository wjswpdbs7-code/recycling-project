# Pico 2 W 통합 하드웨어 테스트 (서보2 + HC-SR04 + LCD1602 I2C)
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

project(pico_hw_test C CXX ASM)
pico_sdk_init()

add_executable(pico_hw_test main.c)

target_link_libraries(pico_hw_test pico_stdlib hardware_pwm hardware_i2c)

pico_enable_stdio_usb(pico_hw_test 1)
pico_enable_stdio_uart(pico_hw_test 0)

pico_add_extra_outputs(pico_hw_test)

# 배선 자동 검사 (원격 판독용, ASCII 출력)
add_executable(pico_hw_check check.c)
target_link_libraries(pico_hw_check pico_stdlib hardware_pwm hardware_i2c)
pico_enable_stdio_usb(pico_hw_check 1)
pico_enable_stdio_uart(pico_hw_check 0)
pico_add_extra_outputs(pico_hw_check)

# I2C 전핀 탐색
add_executable(pico_i2cfind i2cfind.c)
target_link_libraries(pico_i2cfind pico_stdlib hardware_i2c)
pico_enable_stdio_usb(pico_i2cfind 1)
pico_enable_stdio_uart(pico_i2cfind 0)
pico_add_extra_outputs(pico_i2cfind)

# 외부 풀업 탐지 (LCD 신호선 위치)
add_executable(pico_pinfind pinfind.c)
target_link_libraries(pico_pinfind pico_stdlib)
pico_enable_stdio_usb(pico_pinfind 1)
pico_enable_stdio_uart(pico_pinfind 0)
pico_add_extra_outputs(pico_pinfind)

# 초음파 → LCD 실시간 표시 데모
add_executable(sonar_lcd sonar_lcd.c)
target_link_libraries(sonar_lcd pico_stdlib hardware_i2c)
pico_enable_stdio_usb(sonar_lcd 1)
pico_enable_stdio_uart(sonar_lcd 0)
pico_add_extra_outputs(sonar_lcd)

# LCD 핀매핑 진단
add_executable(lcd_test lcd_test.c)
target_link_libraries(lcd_test pico_stdlib hardware_i2c)
pico_enable_stdio_usb(lcd_test 1)
pico_enable_stdio_uart(lcd_test 0)
pico_add_extra_outputs(lcd_test)
