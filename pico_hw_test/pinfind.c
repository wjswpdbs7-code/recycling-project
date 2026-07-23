// 외부 풀업 탐지 — LCD 백팩의 SDA/SCL이 실제로 어느 핀에 닿아 있는지 찾는다.
//
// 원리: I2C 백팩에는 SDA/SCL에 4.7k 풀업 저항이 달려 있다.
//   Pico 내부 풀다운(약 60k)을 걸었을 때도 HIGH로 읽히면
//   = 외부에서 강하게 끌어올리는 것이 붙어 있다 = 백팩 신호선이 그 핀에 닿아 있다.
//
// 출력:
//   PULLUP_PINS: GP4 GP5     ← 이 두 핀에 백팩이 연결됨 (정상 배선)
//   PULLUP_PINS: (none)      ← 신호선이 어디에도 안 닿음 (점퍼 빠짐/단선/백팩 핀 미접촉)
#include <stdio.h>
#include "pico/stdlib.h"

// 사용 중인 핀 제외: GP0/1(서보), GP16/17(초음파), GP23~25,29(내부용)
static const uint SKIP[] = {0, 1, 16, 17, 23, 24, 25, 29};

static bool skipped(uint p) {
    for (unsigned i = 0; i < sizeof(SKIP) / sizeof(SKIP[0]); i++)
        if (SKIP[i] == p) return true;
    return false;
}

int main(void) {
    stdio_init_all();
    sleep_ms(2500);
    printf("\nPULLUP_FIND_START (LCD 백팩 신호선 위치 탐지)\n");

    while (true) {
        printf("PULLUP_PINS:");
        int n = 0;
        for (uint p = 2; p <= 28; p++) {
            if (skipped(p)) continue;
            gpio_init(p);
            gpio_set_dir(p, GPIO_IN);
            gpio_pull_down(p);      // 내부 풀다운으로 끌어내림
            sleep_ms(3);
            if (gpio_get(p)) {      // 그래도 HIGH면 외부 풀업이 존재
                printf(" GP%u", p);
                n++;
            }
            gpio_disable_pulls(p);
        }
        printf(n ? "\n" : " (none)\n");
        printf("COUNT=%d\n", n);
        sleep_ms(2000);
    }
}
