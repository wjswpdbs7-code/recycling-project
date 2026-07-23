// 분리수거 자동분류기 구동부 v2 — 캐러셀(수거함 회전) + 투입구 양문
//
// 기구 구조 (사진 참조 구조):
//   투입구 양문(서보1·3)에 물체가 놓임 → 촬영·판정 →
//   아래 수거함 회전판(서보2)이 해당 칸 각도로 회전 →
//   양문이 반대방향 동기로 열려 낙하 → 닫힘
//
// 배선:
//   서보1 (투입구 왼문)   → GP0 (물리핀 1)
//   서보2 (수거함 회전판) → GP1 (물리핀 2)
//   서보3 (투입구 오른문) → GP2 (물리핀 4)
//   HC-SR04              → TRIG=GP16, ECHO=GP17 (1k/2k 분압)
//   LCD1602 I2C          → SDA=GP4, SCL=GP5
//   서보 전원             → 별도 5~6V(건전지), GND 공통
//
// 시리얼 프로토콜 (상위 Jetson/노트북 ↔ Pico):
//   [수신] SORT PET|CAN|PLASTIC|ETC   회전판 정렬 → 양문 개방 → DONE
//          REJECT <사유>              양문 열지 않고 리턴 안내
//          READY / BUSY / PING / TEST
//   [수동/캘리브레이션]
//          open [양] / close          양문 직접 개폐
//          d1|d2|d3 <각도>            서보 개별 원시 각도
//          cfg [키 값]                설정 (c1 c3 amt b0 b1 b2 b3)
//   [송신] DETECT <cm> / CLEAR <cm> / DONE <칸> / PONG
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/i2c.h"

#define DOOR_L_PIN   0
#define CAROUSEL_PIN 1
#define DOOR_R_PIN   2
#define TRIG_PIN    16
#define ECHO_PIN    17
#define SDA_PIN      4
#define SCL_PIN      5

#define DETECT_CM 15.0f
#define CLEAR_CM  18.0f

// ── 조절 가능 설정 (cfg로 런타임 변경) ──
static int cfg_close_l = 90;                 // 왼문 닫힘
static int cfg_close_r = 90;                 // 오른문 닫힘
static int cfg_amount  = 60;                 // 문 열림 양 (왼 +, 오른 −)
static int cfg_bin[4]  = {0, 60, 120, 180};  // 칸별 회전판 각도
static const char *BIN_NAME[4] = {"PET", "CAN", "PLASTIC", "ETC"};

static int cur_l, cur_r, cur_car;
static int lcd_addr = 0;
#define LCD_BL 0x08

// ── 서보 ──
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

/* 양문 동기 이동 — 1도씩 번갈아, 반대 방향이어도 같은 속도로 동시 도착 */
static void doors_move(int tl, int tr) {
    tl = clamp(tl); tr = clamp(tr);
    while (cur_l != tl || cur_r != tr) {
        if (cur_l != tl) { cur_l += (tl > cur_l) ? 1 : -1; servo_raw(DOOR_L_PIN, cur_l); }
        if (cur_r != tr) { cur_r += (tr > cur_r) ? 1 : -1; servo_raw(DOOR_R_PIN, cur_r); }
        sleep_ms(8);
    }
    sleep_ms(150);
}
static void doors_open(int amt) { doors_move(cfg_close_l + amt, cfg_close_r - amt); }
static void doors_close(void)   { doors_move(cfg_close_l, cfg_close_r); }

/* 회전판 — 수거함 무게 때문에 특히 천천히 (2도/24ms) */
static void carousel_move(int target) {
    target = clamp(target);
    while (cur_car != target) {
        int step = (target > cur_car) ? 2 : -2;
        cur_car += step;
        if ((step > 0 && cur_car > target) || (step < 0 && cur_car < target)) cur_car = target;
        servo_raw(CAROUSEL_PIN, cur_car);
        sleep_ms(24);
    }
    sleep_ms(400);   // 수거함 관성 정착 대기
}

// ── LCD (MAP A, 검증된 타이밍) ──
static void lcd_wr(uint8_t b) { i2c_write_blocking(i2c0, lcd_addr, &b, 1, false); }
static void lcd_nib(uint8_t nib, uint8_t rs) {
    uint8_t base = (nib & 0xF0) | LCD_BL | (rs ? 0x01 : 0);
    lcd_wr(base);        sleep_us(50);
    lcd_wr(base | 0x04); sleep_us(50);
    lcd_wr(base);        sleep_us(150);
}
static void lcd_byte(uint8_t v, uint8_t rs) {
    lcd_nib(v & 0xF0, rs); lcd_nib((v << 4) & 0xF0, rs); sleep_us(150);
}
static void lcd_init(void) {
    sleep_ms(60);
    lcd_nib(0x30, 0); sleep_ms(6);
    lcd_nib(0x30, 0); sleep_ms(6);
    lcd_nib(0x30, 0); sleep_ms(6);
    lcd_nib(0x20, 0); sleep_ms(6);
    lcd_byte(0x28, 0); sleep_ms(2);
    lcd_byte(0x08, 0); sleep_ms(2);
    lcd_byte(0x01, 0); sleep_ms(5);
    lcd_byte(0x06, 0); sleep_ms(2);
    lcd_byte(0x0C, 0); sleep_ms(2);
}
static void lcd_print(int line, const char *s) {
    if (!lcd_addr) return;
    lcd_byte(line == 0 ? 0x80 : 0xC0, 0);
    for (int i = 0; i < 16; i++) lcd_byte(i < (int)strlen(s) ? s[i] : ' ', 1);
}
static void lcd_show(const char *a, const char *b) { lcd_print(0, a); lcd_print(1, b); }

// ── 초음파 ──
static float measure_cm(void) {
    gpio_put(TRIG_PIN, 1); sleep_us(10); gpio_put(TRIG_PIN, 0);
    absolute_time_t dl = make_timeout_time_us(30000);
    while (!gpio_get(ECHO_PIN))
        if (absolute_time_diff_us(get_absolute_time(), dl) < 0) return -1;
    absolute_time_t st = get_absolute_time();
    while (gpio_get(ECHO_PIN))
        if (absolute_time_diff_us(get_absolute_time(), dl) < 0) return -1;
    return (float)absolute_time_diff_us(st, get_absolute_time()) / 58.0f;
}
static float median3(float a, float b, float c) {
    if ((a >= b && a <= c) || (a <= b && a >= c)) return a;
    if ((b >= a && b <= c) || (b <= a && b >= c)) return b;
    return c;
}

// ── 분배 시퀀스 ──
static void do_sort(int bin) {
    char buf[20];
    snprintf(buf, sizeof(buf), "-> %s", BIN_NAME[bin]);
    lcd_show("SORTING", buf);
    printf("분배: %s (회전판 %d도)\n", BIN_NAME[bin], cfg_bin[bin]);

    carousel_move(cfg_bin[bin]);   // 1) 수거함 정렬 (문 열기 전에!)
    doors_open(cfg_amount);        // 2) 양문 개방 → 낙하
    sleep_ms(900);
    doors_close();                 // 3) 양문 닫기

    lcd_show("DONE", buf);
    printf("DONE %s\n", BIN_NAME[bin]);
}

static void show_cfg(void) {
    printf("cfg: c1=%d c3=%d amt=%d | 칸각도 b0(PET)=%d b1(CAN)=%d b2(PLASTIC)=%d b3(ETC)=%d\n",
           cfg_close_l, cfg_close_r, cfg_amount, cfg_bin[0], cfg_bin[1], cfg_bin[2], cfg_bin[3]);
    printf("현재: 왼문=%d 오른문=%d 회전판=%d\n", cur_l, cur_r, cur_car);
}

static void run_cmd(char *line) {
    if (!strncmp(line, "SORT ", 5) || !strncmp(line, "sort ", 5)) {
        const char *w = line + 5;
        for (int i = 0; i < 4; i++)
            if (!strcasecmp(w, BIN_NAME[i])) { do_sort(i); return; }
        printf("알 수 없는 칸: %s\n", w);
    }
    else if (!strncasecmp(line, "REJECT", 6)) {
        lcd_show("RETURN", line[6] ? line + 7 : "");
        printf("리턴 안내 (양문 유지)\n");
    }
    else if (!strcasecmp(line, "READY")) { lcd_show("READY", "PUT ONE ITEM"); printf("대기\n"); }
    else if (!strcasecmp(line, "BUSY"))  { lcd_show("ANALYZING...", "PLEASE WAIT"); }
    else if (!strcasecmp(line, "PING"))  { printf("PONG\n"); }
    else if (!strcasecmp(line, "TEST")) {
        printf("자가 점검: 4칸 순회 + 양문 개폐\n");
        for (int i = 0; i < 4; i++) {
            char b[20]; snprintf(b, sizeof(b), "TEST %s", BIN_NAME[i]);
            lcd_show(b, "");
            carousel_move(cfg_bin[i]);
            doors_open(cfg_amount); sleep_ms(500);
            doors_close();          sleep_ms(300);
        }
        carousel_move(cfg_bin[0]);
        lcd_show("TEST DONE", "");
        printf("점검 완료\n");
    }
    // ── 수동/캘리브레이션 ──
    else if (!strncmp(line, "open", 4)) {
        int amt = (line[4] == ' ') ? atoi(line + 5) : cfg_amount;
        printf("양문 열기 %d (반대방향 동기)\n", amt);
        doors_open(amt); printf("완료\n");
    }
    else if (!strcmp(line, "close")) { doors_close(); printf("양문 닫힘\n"); }
    else if (!strncmp(line, "d1 ", 3)) { doors_move(atoi(line + 3), cur_r); printf("왼문=%d\n", cur_l); }
    else if (!strncmp(line, "d2 ", 3)) { carousel_move(atoi(line + 3)); printf("회전판=%d\n", cur_car); }
    else if (!strncmp(line, "d3 ", 3)) { doors_move(cur_l, atoi(line + 3)); printf("오른문=%d\n", cur_r); }
    else if (!strcmp(line, "cfg")) show_cfg();
    else if (!strncmp(line, "cfg ", 4)) {
        char key[8]; int v;
        if (sscanf(line + 4, "%7s %d", key, &v) == 2) {
            if (!strcmp(key, "c1")) cfg_close_l = clamp(v);
            else if (!strcmp(key, "c3")) cfg_close_r = clamp(v);
            else if (!strcmp(key, "amt")) cfg_amount = v;
            else if (key[0] == 'b' && key[1] >= '0' && key[1] <= '3' && !key[2])
                cfg_bin[key[1] - '0'] = clamp(v);
            else { printf("키: c1 c3 amt b0 b1 b2 b3\n"); return; }
            show_cfg();
        } else printf("사용법: cfg <키> <값>\n");
    }
    else if (!strcmp(line, "help")) {
        printf("SORT PET|CAN|PLASTIC|ETC / REJECT / READY / BUSY / TEST / PING\n");
        printf("open [양] / close / d1|d2|d3 <각> / cfg [키 값]\n");
    }
    else if (strlen(line)) printf("모르는 명령: %s\n", line);
}

int main(void) {
    stdio_init_all();

    servo_init(DOOR_L_PIN);
    servo_init(CAROUSEL_PIN);
    servo_init(DOOR_R_PIN);
    cur_l = cfg_close_l; cur_r = cfg_close_r; cur_car = cfg_bin[0];
    servo_raw(DOOR_L_PIN, cur_l);
    servo_raw(CAROUSEL_PIN, cur_car);
    servo_raw(DOOR_R_PIN, cur_r);

    gpio_init(TRIG_PIN); gpio_set_dir(TRIG_PIN, GPIO_OUT); gpio_put(TRIG_PIN, 0);
    gpio_init(ECHO_PIN); gpio_set_dir(ECHO_PIN, GPIO_IN);

    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    sleep_ms(1500);
    for (int a = 0x08; a < 0x78 && !lcd_addr; a++) {
        uint8_t rx;
        if (i2c_read_timeout_us(i2c0, a, &rx, 1, false, 2000) >= 0) lcd_addr = a;
    }
    if (lcd_addr) { lcd_init(); lcd_show("SORTER READY", "PUT ONE ITEM"); }

    printf("\n=== 분류기 구동부 v2 (캐러셀+양문, LCD 0x%02X) ===\n", lcd_addr);
    show_cfg();

    char buf[64];
    int n = 0;
    bool detected = false;
    absolute_time_t next_scan = get_absolute_time();

    while (true) {
        int c = getchar_timeout_us(0);
        while (c != PICO_ERROR_TIMEOUT) {
            if (c == '\r' || c == '\n') {
                buf[n] = 0;
                if (n) { printf("\n"); run_cmd(buf); }
                n = 0;
            } else if ((c == 8 || c == 127) && n > 0) {
                n--; printf("\b \b");
            } else if (n < 63 && c >= 32) {
                buf[n++] = (char)c;
                putchar(c);   // 에코 (수동 조작 시 입력이 보이게)
            }
            c = getchar_timeout_us(0);
        }

        if (absolute_time_diff_us(get_absolute_time(), next_scan) < 0) {
            next_scan = make_timeout_time_ms(200);
            float d = median3(measure_cm(), measure_cm(), measure_cm());
            if (d > 0) {
                if (d < DETECT_CM && !detected) {
                    detected = true;
                    printf("DETECT %.1f\n", d);
                    lcd_show("DETECTED", "ANALYZING...");
                } else if (d >= CLEAR_CM && detected) {
                    detected = false;
                    printf("CLEAR %.1f\n", d);
                    lcd_show("READY", "PUT ONE ITEM");
                }
            }
        }
    }
}
