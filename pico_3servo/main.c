// 서보 3개 테스트 — 투입구 양문(1·3번, 반대 방향 동기) + 셔터(2번)
//
// 배선:
//   서보1 (투입구 왼문)  신호 → GP0 (물리핀 1)
//   서보2 (셔터)         신호 → GP1 (물리핀 2)
//   서보3 (투입구 오른문) 신호 → GP2 (물리핀 4)   ← 새로 추가
//   전원: 3개 모두 별도 5~6V(건전지), GND 공통
//
// ★ 투입구 양문은 서로 마주보게 장착되므로, 같은 양만큼 열려면
//   왼문은 +방향, 오른문은 −방향으로 "반대로" 움직여야 한다.
//   open/close 명령이 이를 자동 처리하며, 두 문은 1도씩 번갈아
//   같은 속도로 동기 이동한다.
//
// 시리얼 명령 (115200, 엔터로 실행):
//   open [양]     투입구 열기 (기본 60도만큼, 양문 반대방향 동기)
//   close         투입구 닫기
//   shopen        셔터 열기 / shclose  셔터 닫기
//   d1 <0~180>    서보1 원시 각도 (캘리브레이션용 개별 제어)
//   d2 <0~180>    서보2(셔터) 원시 각도
//   d3 <0~180>    서보3 원시 각도
//   cfg           현재 설정 보기
//   cfg c1 <각>   왼문 닫힘 각도 설정   (기본 90)
//   cfg c3 <각>   오른문 닫힘 각도 설정 (기본 90)
//   cfg amt <각>  열림 양 설정          (기본 60)
//   cfg so <각>   셔터 열림 각도        (기본 100)
//   cfg sc <각>   셔터 닫힘 각도        (기본 10)
//   demo          전체 사이클 시연 (문 열기→닫기→셔터 열기→닫기)
//   help          명령 목록
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"

#define DOOR_L_PIN 0   // 서보1: 투입구 왼문
#define SHUTTER_PIN 1  // 서보2: 셔터
#define DOOR_R_PIN 2   // 서보3: 투입구 오른문

// ── 조절 가능한 설정 (cfg 명령으로 런타임 변경) ──
static int cfg_close_l = 90;   // 왼문 닫힘 각도
static int cfg_close_r = 90;   // 오른문 닫힘 각도
static int cfg_amount  = 60;   // 열림 양 (왼문 +, 오른문 −)
static int cfg_sh_open  = 100; // 셔터 열림
static int cfg_sh_close = 10;  // 셔터 닫힘

static int cur_l, cur_r, cur_sh;   // 현재 각도 추적

static void servo_init(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(slice, 150.0f);
    pwm_set_wrap(slice, 20000);
    pwm_set_enabled(slice, true);
}

static int clamp(int v) { return v < 0 ? 0 : (v > 180 ? 180 : v); }

static void servo_raw(uint pin, int deg) {
    pwm_set_gpio_level(pin, 500 + clamp(deg) * 2000 / 180);
}

/* 양문 동기 이동 — 두 문을 1도씩 번갈아 움직여 같은 속도 유지.
   목표: 왼문 tl, 오른문 tr. 서로 반대 방향이어도 동시에 도착한다. */
static void doors_move(int tl, int tr) {
    tl = clamp(tl); tr = clamp(tr);
    while (cur_l != tl || cur_r != tr) {
        if (cur_l != tl) { cur_l += (tl > cur_l) ? 1 : -1; servo_raw(DOOR_L_PIN, cur_l); }
        if (cur_r != tr) { cur_r += (tr > cur_r) ? 1 : -1; servo_raw(DOOR_R_PIN, cur_r); }
        sleep_ms(8);   // 1도당 8ms — 60도 이동에 약 0.5초
    }
    sleep_ms(150);
}

static void shutter_move(int t) {
    t = clamp(t);
    while (cur_sh != t) {
        cur_sh += (t > cur_sh) ? 1 : -1;
        servo_raw(SHUTTER_PIN, cur_sh);
        sleep_ms(6);
    }
    sleep_ms(150);
}

static void doors_open(int amt) {
    // ★ 반대 방향: 왼문은 닫힘각 + amt, 오른문은 닫힘각 − amt
    printf("투입구 열기: 왼문 %d→%d, 오른문 %d→%d (반대방향 동기)\n",
           cur_l, cfg_close_l + amt, cur_r, cfg_close_r - amt);
    doors_move(cfg_close_l + amt, cfg_close_r - amt);
    printf("열림 완료\n");
}

static void doors_close(void) {
    printf("투입구 닫기: 왼문 %d→%d, 오른문 %d→%d\n", cur_l, cfg_close_l, cur_r, cfg_close_r);
    doors_move(cfg_close_l, cfg_close_r);
    printf("닫힘 완료\n");
}

static void show_cfg(void) {
    printf("설정: 닫힘 c1=%d c3=%d / 열림양 amt=%d / 셔터 so=%d sc=%d\n",
           cfg_close_l, cfg_close_r, cfg_amount, cfg_sh_open, cfg_sh_close);
    printf("현재 각도: 왼문=%d 오른문=%d 셔터=%d\n", cur_l, cur_r, cur_sh);
}

static void cmd_help(void) {
    printf("명령: open [양] / close / shopen / shclose / d1|d2|d3 <각도>\n");
    printf("      cfg [c1|c3|amt|so|sc <각도>] / demo / help\n");
}

static void run_cmd(char *line) {
    if (!strcmp(line, "help")) cmd_help();
    else if (!strncmp(line, "open", 4)) {
        int amt = (line[4] == ' ') ? atoi(line + 5) : cfg_amount;
        doors_open(amt);
    }
    else if (!strcmp(line, "close")) doors_close();
    else if (!strcmp(line, "shopen"))  { printf("셔터 열기 → %d도\n", cfg_sh_open);  shutter_move(cfg_sh_open);  printf("완료\n"); }
    else if (!strcmp(line, "shclose")) { printf("셔터 닫기 → %d도\n", cfg_sh_close); shutter_move(cfg_sh_close); printf("완료\n"); }
    else if (!strncmp(line, "d1 ", 3)) { int a = atoi(line + 3); printf("서보1(왼문) → %d도\n", a); doors_move(a, cur_r); }
    else if (!strncmp(line, "d2 ", 3)) { int a = atoi(line + 3); printf("서보2(셔터) → %d도\n", a); shutter_move(a); }
    else if (!strncmp(line, "d3 ", 3)) { int a = atoi(line + 3); printf("서보3(오른문) → %d도\n", a); doors_move(cur_l, a); }
    else if (!strcmp(line, "cfg")) show_cfg();
    else if (!strncmp(line, "cfg ", 4)) {
        char key[8]; int v;
        if (sscanf(line + 4, "%7s %d", key, &v) == 2) {
            if (!strcmp(key, "c1")) cfg_close_l = clamp(v);
            else if (!strcmp(key, "c3")) cfg_close_r = clamp(v);
            else if (!strcmp(key, "amt")) cfg_amount = v;
            else if (!strcmp(key, "so")) cfg_sh_open = clamp(v);
            else if (!strcmp(key, "sc")) cfg_sh_close = clamp(v);
            else { printf("모르는 키: %s\n", key); return; }
            show_cfg();
        } else printf("사용법: cfg c1|c3|amt|so|sc <각도>\n");
    }
    else if (!strcmp(line, "demo")) {
        printf("=== 전체 사이클 시연 ===\n");
        doors_open(cfg_amount);  sleep_ms(700);
        doors_close();           sleep_ms(500);
        shutter_move(cfg_sh_open);  printf("셔터 열림\n"); sleep_ms(900);
        shutter_move(cfg_sh_close); printf("셔터 닫힘\n");
        printf("=== 시연 끝 ===\n");
    }
    else if (strlen(line)) printf("모르는 명령: %s (help 입력)\n", line);
}

int main(void) {
    stdio_init_all();

    servo_init(DOOR_L_PIN);
    servo_init(SHUTTER_PIN);
    servo_init(DOOR_R_PIN);
    cur_l = cfg_close_l;
    cur_r = cfg_close_r;
    cur_sh = cfg_sh_close;
    servo_raw(DOOR_L_PIN, cur_l);
    servo_raw(SHUTTER_PIN, cur_sh);
    servo_raw(DOOR_R_PIN, cur_r);

    sleep_ms(2500);
    printf("\n=== 서보 3개 테스트 (투입구 양문 + 셔터) ===\n");
    cmd_help();
    show_cfg();

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
