// HC-SR04 초음파 거리 테스트 (Pico 2 W)
//
// 배선 (⚠ ECHO는 반드시 분압 저항 거쳐서!):
//   VCC  → VBUS(40번 핀, 5V)   ※ 3V3에 물리면 동작 불안정
//   GND  → GND(38번 핀)
//   TRIG → GP16(21번 핀)        Pico 3.3V 출력이지만 HC-SR04가 HIGH로 인식 → 직결 OK
//   ECHO → 1kΩ ─ GP17(22번 핀) ─ 2kΩ ─ GND   (5V→3.3V 분압. 직결 금지: GPIO 손상)
//
// 동작: 10cm 이내에 물체가 들어오면 "DETECT" 이벤트 출력.
//       평상시 5회/초 측정, 거리를 USB 시리얼로 출력.
// (Pico 2 W 내장 LED는 무선칩 소속이라 cyw43 드라이버가 필요한데,
//  현재 SDK에 해당 서브모듈이 없어 LED 없이 시리얼 출력만 사용)
#include <stdio.h>
#include "pico/stdlib.h"

#define TRIG_PIN 16
#define ECHO_PIN 17
#define DETECT_CM 10.0f   // 투입 감지 임계 거리
#define TIMEOUT_US 30000  // 30ms = 약 5m 왕복. 초과 시 측정 실패

// 1회 측정: 거리(cm) 반환, 실패 시 -1
static float measure_cm(void) {
    // 10us 트리거 펄스 → 센서가 초음파 8펄스 발사 후 ECHO를 왕복시간만큼 HIGH
    gpio_put(TRIG_PIN, 1);
    sleep_us(10);
    gpio_put(TRIG_PIN, 0);

    absolute_time_t deadline = make_timeout_time_us(TIMEOUT_US);
    while (!gpio_get(ECHO_PIN)) {                 // ECHO 상승 대기
        if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) return -1;
    }
    absolute_time_t start = get_absolute_time();
    while (gpio_get(ECHO_PIN)) {                  // ECHO 하강 대기
        if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) return -1;
    }
    int64_t us = absolute_time_diff_us(start, get_absolute_time());
    return (float)us / 58.0f;  // 왕복시간(us) ÷ 58 = 거리(cm), 음속 343m/s 기준
}

int main(void) {
    stdio_init_all();

    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);
    gpio_put(TRIG_PIN, 0);
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);

    sleep_ms(2000);  // USB 시리얼 연결 대기
    printf("HC-SR04 테스트 시작 (임계 %.0fcm)\n", DETECT_CM);

    bool detected = false;  // 상태 기억 → 연속 DETECT 도배 방지
    while (true) {
        // 3회 측정 중앙값 — 초음파 노이즈(연질 표면 반사 불량 등) 완화
        float a = measure_cm(), b = measure_cm(), c = measure_cm();
        float d;
        if (a < 0 || b < 0 || c < 0) d = -1;
        else d = (a > b) == (a < c) ? a : ((b > a) == (b < c) ? b : c);

        if (d < 0) {
            printf("측정 실패 (에코 없음 — 배선/전원 확인)\n");
        } else if (d < DETECT_CM && !detected) {
            detected = true;
            printf("DETECT %.1fcm\n", d);          // ← 상위(노트북)가 이 줄로 촬영 트리거
        } else if (d >= DETECT_CM + 2 && detected) {  // +2cm 히스테리시스로 경계 떨림 방지
            detected = false;
            printf("CLEAR %.1fcm\n", d);
        } else {
            printf("거리 %.1fcm\n", d);
        }
        sleep_ms(200);
    }
}
