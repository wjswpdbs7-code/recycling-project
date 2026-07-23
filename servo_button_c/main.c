// Pico 2 W + SG90 서보 2개 + 버튼 (C / Pico SDK 버전)
// 버튼을 누를 때마다 두 서보가 45도씩 회전 (0 → 45 → 90 → 135 → 180 → 0 반복)
//
// 배선 (MicroPython 버전과 동일):
//   서보1 신호(주황) → GP0  (물리핀 1)
//   서보2 신호(주황) → GP1  (물리핀 2)
//   버튼 한쪽       → GP15 (물리핀 20), 반대쪽 → GND
//   서보 전원(빨강)  → 외부 5V (VBUS 물리핀 40도 가능하나 서보 2개면 외부 전원 권장)
//   서보 GND(갈색)  → Pico GND와 반드시 공통

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#define SERVO1_PIN 0   // GP0
#define SERVO2_PIN 1   // GP1
#define BUTTON_PIN 15  // GP15

// ---------- 서보 PWM 설정 (SG90: 50Hz, 0.5ms~2.5ms 펄스) ----------
// PWM 카운터를 1틱 = 1마이크로초(us)로 맞추고, 20000틱(20ms) 주기로 반복시킨다.
// 그러면 duty 값 = 펄스폭(us) 이 되어 계산이 아주 쉬워진다.
//   0도   = 500us 펄스
//   180도 = 2500us 펄스

static void servo_init(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);            // 핀을 PWM 기능으로 전환
    uint slice = pwm_gpio_to_slice_num(pin);          // 이 핀이 속한 PWM 슬라이스 번호
    float div = clock_get_hz(clk_sys) / 1000000.0f;   // 시스템클럭(150MHz)을 1MHz로 분주 → 1틱=1us
    pwm_set_clkdiv(slice, div);
    pwm_set_wrap(slice, 20000 - 1);                   // 20000us = 20ms 주기 = 50Hz
    pwm_set_enabled(slice, true);
}

static void servo_set_angle(uint pin, int angle) {
    if (angle < 0)   angle = 0;
    if (angle > 180) angle = 180;
    uint16_t pulse_us = 500 + (uint32_t)(2500 - 500) * angle / 180;
    pwm_set_gpio_level(pin, pulse_us);                // duty 값 = 펄스폭(us)
}

int main(void) {
    stdio_init_all();  // USB 시리얼로 printf 출력 가능하게

    // 버튼: 내부 풀업 사용 → 누르면 0(LOW)
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    servo_init(SERVO1_PIN);
    servo_init(SERVO2_PIN);

    int angle = 0;
    servo_set_angle(SERVO1_PIN, angle);
    servo_set_angle(SERVO2_PIN, angle);
    printf("시작: 0도. 버튼을 누르면 45도씩 회전합니다.\n");

    while (true) {
        if (gpio_get(BUTTON_PIN) == 0) {      // 버튼 눌림 감지
            sleep_ms(20);                     // 디바운스(채터링 방지)
            if (gpio_get(BUTTON_PIN) == 0) {
                angle += 45;
                if (angle > 180) angle = 0;   // 180도 넘으면 0도로 복귀
                servo_set_angle(SERVO1_PIN, angle);
                servo_set_angle(SERVO2_PIN, angle);
                printf("현재 각도: %d\n", angle);
                // 버튼에서 손을 뗄 때까지 대기 (한 번 누름 = 한 번 회전)
                while (gpio_get(BUTTON_PIN) == 0) {
                    sleep_ms(10);
                }
                sleep_ms(50);                 // 뗄 때 채터링 방지
            }
        }
        sleep_ms(10);
    }
}
