// 투입구 양문 테스트 — 적정 열림 각도 탐색용
//
// 배선: 왼문=GP0(물리핀1), 오른문=GP2(물리핀4). 전원 별도 5~6V, GND 공통.
// ★ 부팅 시 두 서보 모두 0도로 초기화된다.
//   (0도 = 혼 장착 기준각. 이 상태에서 문짝을 "닫힘" 위치로 붙이면 된다)
//
// 양문은 마주보고 장착되므로 열 때 서로 반대 방향으로 움직인다:
//   왼문 = 닫힘각 + 열림양,  오른문 = 닫힘각 − 열림양  (1도씩 동기 이동)
//
// 시리얼 명령 (115200, 엔터 실행):
//   open        기본 열림양(amt)만큼 열기
//   open 45     45도만큼 열기 (열림양 임시 지정 — 적정 각도 탐색용)
//   close       닫기 (닫힘각으로 복귀)
//   d1 <각도>   왼문만 원시 각도 이동 (0~180)
//   d3 <각도>   오른문만 원시 각도 이동
//   zero        두 문 모두 0도로 (초기 상태)
//   cfg         설정·현재 각도 보기
//   cfg c1 <각> 왼문 닫힘각 설정 (기본 0)
//   cfg c3 <각> 오른문 닫힘각 설정 (기본 0)
//   cfg amt <각> 기본 열림양 설정 (기본 60)
//   help        명령 목록
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"

#define DOOR_L_PIN 0
#define DOOR_R_PIN 2

// 닫힘 기준각 — 부팅 초기값 0도 (요청사항)
static int cfg_close_l = 0;
static int cfg_close_r = 0;
static int cfg_amount  = 60;
static int dir_r = +1;   // 오른문 열림 방향 (+1/-1, flip 명령으로 전환)

static int cur_l = 0, cur_r = 0;

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

/* 양문 동기 이동 — 1도씩 번갈아, 반대 방향이어도 같은 속도 */
static void doors_move(int tl, int tr) {
    tl = clamp(tl); tr = clamp(tr);
    while (cur_l != tl || cur_r != tr) {
        if (cur_l != tl) { cur_l += (tl > cur_l) ? 1 : -1; servo_raw(DOOR_L_PIN, cur_l); }
        if (cur_r != tr) { cur_r += (tr > cur_r) ? 1 : -1; servo_raw(DOOR_R_PIN, cur_r); }
        sleep_ms(8);
    }
    sleep_ms(120);
}

static void show(void) {
    printf("설정: 닫힘 c1=%d c3=%d, 열림양 amt=%d, dir_r=%+d | 현재: 왼문=%d 오른문=%d\n",
           cfg_close_l, cfg_close_r, cfg_amount, dir_r, cur_l, cur_r);
}

static void doors_open(int amt) {
    int raw_r = cfg_close_r + dir_r * amt;
    int tl = clamp(cfg_close_l + amt);
    int tr = clamp(raw_r);
    if (raw_r < 0 || raw_r > 180)
        printf("경고: 오른문이 %d도 한계에 걸림 → flip 또는 cfg c3 조정\n", raw_r < 0 ? 0 : 180);
    printf("열기 %d도: 왼문 %d→%d, 오른문 %d→%d (dir_r=%+d)\n", amt, cur_l, tl, cur_r, tr, dir_r);
    doors_move(tl, tr);
    printf("완료\n");
}

static void cmd_help(void) {
    printf("명령: open [양] / close / d1 <각> / d3 <각> / zero / flip / cfg [c1|c3|amt <각>] / help\n");
}

static void run_cmd(char *line) {
    if (!strcmp(line, "help")) cmd_help();
    else if (!strncmp(line, "open", 4)) {
        int amt = (line[4] == ' ') ? atoi(line + 5) : cfg_amount;
        doors_open(amt);
    }
    else if (!strcmp(line, "close")) {
        printf("닫기: 왼문 %d→%d, 오른문 %d→%d\n", cur_l, cfg_close_l, cur_r, cfg_close_r);
        doors_move(cfg_close_l, cfg_close_r);
        printf("완료\n");
    }
    else if (!strcmp(line, "zero")) { doors_move(0, 0); printf("양문 0도\n"); }
    else if (!strcmp(line, "flip")) {
        dir_r = -dir_r;
        printf("오른문 방향 전환: dir_r=%+d (open 시 오른문 = c3 %s 열림양)\n",
               dir_r, dir_r > 0 ? "+" : "-");
    }
    else if (!strncmp(line, "d1 ", 3)) { doors_move(atoi(line + 3), cur_r); printf("왼문=%d\n", cur_l); }
    else if (!strncmp(line, "d3 ", 3)) { doors_move(cur_l, atoi(line + 3)); printf("오른문=%d\n", cur_r); }
    else if (!strcmp(line, "cfg")) show();
    else if (!strncmp(line, "cfg ", 4)) {
        char key[8]; int v;
        if (sscanf(line + 4, "%7s %d", key, &v) == 2) {
            if (!strcmp(key, "c1")) cfg_close_l = clamp(v);
            else if (!strcmp(key, "c3")) cfg_close_r = clamp(v);
            else if (!strcmp(key, "amt")) cfg_amount = v;
            else { printf("키: c1 c3 amt\n"); return; }
            show();
        } else printf("사용법: cfg c1|c3|amt <각도>\n");
    }
    else if (strlen(line)) printf("모르는 명령: %s (help)\n", line);
}

int main(void) {
    stdio_init_all();

    servo_init(DOOR_L_PIN);
    servo_init(DOOR_R_PIN);
    // ★ 부팅 시 0도 초기화
    cur_l = 0; cur_r = 0;
    servo_raw(DOOR_L_PIN, 0);
    servo_raw(DOOR_R_PIN, 0);

    sleep_ms(2500);
    printf("\n=== 투입구 양문 테스트 (부팅 시 양문 0도 초기화) ===\n");
    cmd_help();
    show();

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
