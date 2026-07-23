// HC-SR04 배선 진단 펌웨어 — 1초마다 3가지 테스트 결과를 한국어로 출력
//   [A] GP17 내부 풀업 테스트: 분압(2k→GND) 경로가 GP17에 붙어 있는지
//   [B] 평상시 ECHO 레벨: 0이어야 정상 (1이면 배선 오류/직결 의심)
//   [C] TRIG 발사 후 40ms 동안 ECHO 상승·하강 관찰: 센서 응답 유무·펄스 폭
#include <stdio.h>
#include "pico/stdlib.h"

#define TRIG_PIN 16
#define ECHO_PIN 17

int main(void) {
    stdio_init_all();
    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);
    gpio_put(TRIG_PIN, 0);
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);

    sleep_ms(3000);
    printf("=== HC-SR04 배선 진단 시작 (TRIG=GP16, ECHO=GP17) ===\n");

    int round = 0;
    while (true) {
        printf("\n--- 진단 #%d ---\n", ++round);

        // [A] 내부 풀업을 걸었을 때 LOW면 = 외부에서 뭔가(2k→GND)가 끌어내리고 있음 = 연결됨
        gpio_pull_up(ECHO_PIN);
        sleep_ms(5);
        int up = gpio_get(ECHO_PIN);
        gpio_disable_pulls(ECHO_PIN);
        sleep_ms(5);
        printf("[A] 풀업시 ECHO=%d → %s\n", up,
               up ? "GP17이 허공에 떠 있음! (분압 중간점→GP17 점퍼 또는 2k→GND 경로 끊김)"
                  : "분압 회로가 GP17에 연결되어 있음 (정상)");

        // [B] 평상시 레벨
        int idle = gpio_get(ECHO_PIN);
        printf("[B] 평상시 ECHO=%d → %s\n", idle,
               idle ? "비정상: 계속 HIGH (배선 오류 — ECHO 경로 재확인)" : "정상 (LOW)");

        // [C] 트리거 발사 후 관찰
        gpio_put(TRIG_PIN, 1);
        sleep_us(10);
        gpio_put(TRIG_PIN, 0);
        absolute_time_t t0 = get_absolute_time();
        int64_t rise_us = -1, fall_us = -1;
        while (absolute_time_diff_us(t0, get_absolute_time()) < 40000) {
            if (gpio_get(ECHO_PIN)) { rise_us = absolute_time_diff_us(t0, get_absolute_time()); break; }
        }
        if (rise_us >= 0) {
            while (absolute_time_diff_us(t0, get_absolute_time()) < 40000) {
                if (!gpio_get(ECHO_PIN)) { fall_us = absolute_time_diff_us(t0, get_absolute_time()); break; }
            }
        }
        if (rise_us < 0)
            printf("[C] 트리거 후 ECHO 상승 없음 → 센서가 응답 안 함\n"
                   "    의심 순서: ①센서 VCC가 5V(VBUS)인지 ②센서쪽 4핀 순서(VCC-TRIG-ECHO-GND)\n"
                   "    ③TRIG 점퍼(GP16→센서) ④저항 순서 뒤바뀜(2k가 ECHO쪽이면 전압부족)\n");
        else if (fall_us < 0)
            printf("[C] ECHO 상승 후 40ms 내 하강 없음 → 센서 앞 장애물 너무 멀거나 반사 없음\n");
        else
            printf("[C] 정상 펄스! 폭 %lldus = 거리 %.1fcm → 배선 이상 없음\n",
                   fall_us - rise_us, (double)(fall_us - rise_us) / 58.0);

        sleep_ms(1000);
    }
}
