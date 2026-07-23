// 서보 전수 테스트 — 3개(GP0/GP1/GP2) 개별·동시·자동 점검
//
// 배선: 서보1=GP0(물리핀1), 서보2=GP1(물리핀2), 서보3=GP2(물리핀4)
//       전원은 별도 5~6V(건전지), GND 공통. 신호만 Pico.
//
// 시리얼 명령 (115200, 엔터 실행):
//   s1 90 / s2 45 / s3 180   개별 각도 (0~180)
//   all 90                   3개 동시에 같은 각도
//   center                   3개 전부 90도
//   sweep                    1→2→3 순서로 0~180 왕복 (개별 왕복)
//   sweepall                 3개 동시에 0~180 왕복 (전류 최대 부하 시험!)
//   auto                     자동 점검: 각 서보 60↔120 꿈틀 → 판정 출력
//   status                   현재 각도 표시
//   help                     명령 목록
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"

static const uint PIN[3] = {0, 1, 2};
static int cur[3] = {90, 90, 90};

static void servo_init(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(slice, 150.0f);
    pwm_set_wrap(slice, 20000);
    pwm_set_enabled(slice, true);
}
static int clamp(int v) { return v < 0 ? 0 : (v > 180 ? 180 : v); }
static void raw(int idx, int deg) {
    cur[idx] = clamp(deg);
    pwm_set_gpio_level(PIN[idx], 500 + cur[idx] * 2000 / 180);
}

/* 한 서보를 천천히 이동 (3도/20ms) */
static void slow(int idx, int target) {
    target = clamp(target);
    while (cur[idx] != target) {
        int step = (target > cur[idx]) ? 3 : -3;
        int next = cur[idx] + step;
        if ((step > 0 && next > target) || (step < 0 && next < target)) next = target;
        raw(idx, next);
        sleep_ms(20);
    }
}

/* 3개 동시 이동 — 1도씩 번갈아 */
static void slow_all(int target) {
    target = clamp(target);
    bool moving = true;
    while (moving) {
        moving = false;
        for (int i = 0; i < 3; i++) {
            if (cur[i] != target) {
                raw(i, cur[i] + ((target > cur[i]) ? 1 : -1));
                moving = true;
            }
        }
        sleep_ms(8);
    }
}

static void status(void) {
    printf("각도: 서보1=%d 서보2=%d 서보3=%d\n", cur[0], cur[1], cur[2]);
}

static void cmd_help(void) {
    printf("명령: s1|s2|s3 <각도> / all <각도> / center / sweep / sweepall / auto / status / help\n");
}

static void run_cmd(char *line) {
    if (!strcmp(line, "help")) cmd_help();
    else if (!strcmp(line, "status")) status();
    else if (!strcmp(line, "center")) { slow_all(90); printf("전부 90도\n"); }
    else if ((line[0] == 's') && (line[1] >= '1' && line[1] <= '3') && line[2] == ' ') {
        int idx = line[1] - '1';
        int deg = atoi(line + 3);
        printf("서보%d → %d도\n", idx + 1, clamp(deg));
        slow(idx, deg);
        printf("완료\n");
    }
    else if (!strncmp(line, "all ", 4)) {
        int deg = atoi(line + 4);
        printf("3개 동시 → %d도\n", clamp(deg));
        slow_all(deg);
        printf("완료\n");
    }
    else if (!strcmp(line, "sweep")) {
        for (int i = 0; i < 3; i++) {
            printf("서보%d 왕복 (0→180→90)...\n", i + 1);
            slow(i, 0); sleep_ms(300);
            slow(i, 180); sleep_ms(300);
            slow(i, 90);
        }
        printf("개별 왕복 완료\n");
    }
    else if (!strcmp(line, "sweepall")) {
        printf("3개 동시 왕복 — 전류 최대 부하 시험 (리셋되면 전원 부족)\n");
        slow_all(0); sleep_ms(300);
        slow_all(180); sleep_ms(300);
        slow_all(90);
        printf("동시 왕복 완료 (리셋 없이 끝났으면 전원 합격)\n");
    }
    else if (!strcmp(line, "auto")) {
        printf("=== 자동 점검: 각 서보 60↔120 꿈틀 ===\n");
        for (int i = 0; i < 3; i++) {
            printf("[%d/3] 서보%d (GP%d) 동작 중 — 움직이는지 육안 확인\n", i + 1, i + 1, PIN[i]);
            slow(i, 60); sleep_ms(250);
            slow(i, 120); sleep_ms(250);
            slow(i, 90);
            sleep_ms(400);
        }
        printf("=== 점검 끝 — 안 움직인 서보는 신호선(GP핀)·전원·GND공통 확인 ===\n");
    }
    else if (strlen(line)) printf("모르는 명령: %s (help)\n", line);
}

int main(void) {
    stdio_init_all();
    for (int i = 0; i < 3; i++) { servo_init(PIN[i]); raw(i, 90); }

    sleep_ms(2500);
    printf("\n=== 서보 전수 테스트 (GP0/GP1/GP2) ===\n");
    cmd_help();
    status();

    char buf[64];
    int n = 0;
    while (true) {
        int c = getchar_timeout_us(100000);
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (c == '\r' || c == '\n') {
            printf("\n");
            buf[n] = 0;
            if (n) run_cmd(buf);
            n = 0;
            printf("> ");
        } else if ((c == 8 || c == 127) && n > 0) {
            n--; printf("\b \b");
        } else if (n < 63 && c >= 32) {
            buf[n++] = (char)c;
            putchar(c);
        }
    }
}
