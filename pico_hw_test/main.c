// Pico 2 W 통합 하드웨어 테스트 — 시리얼 모니터로 전 장치 조작·확인
//
// 배선:
//   서보1 신호  → GP0  (물리핀 1)   ┐ 서보 전원(빨강)은 별도 5V, GND 공통!
//   서보2 신호  → GP1  (물리핀 2)   ┘
//   HC-SR04     → TRIG=GP16, ECHO=GP17(1k/2k 분압) — 기존 배선 그대로
//   LCD1602 I2C → SDA=GP4 (물리핀 6), SCL=GP5 (물리핀 7), VCC=3V3(우선)→흐리면 VBUS
//
// PuTTY(COM, 115200)에서 명령 입력 (엔터로 실행):
//   help          명령 목록
//   scan          I2C 스캔 (LCD 주소 찾기 — 0x27 또는 0x3F가 정상)
//   dist          초음파 1회 측정
//   watch         초음파 연속 측정 20회
//   s1 90         서보1을 90도로 (0~180)
//   s2 45         서보2를 45도로
//   sweep         서보 두 개 왕복 데모
//   lcd TEXT      LCD 1줄에 TEXT 표시 (영문/숫자만)
//   lcd2 TEXT     LCD 2줄에 표시
//   all           전 장치 자동 점검 (스캔→LCD→초음파→서보 꿈틀)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/i2c.h"

#define SERVO1_PIN 0
#define SERVO2_PIN 1
#define TRIG_PIN 16
#define ECHO_PIN 17
#define SDA_PIN 4
#define SCL_PIN 5

// ---------- 서보 (50Hz PWM, 500~2500us) ----------
static void servo_init(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(slice, 150.0f);   // 150MHz/150 = 1MHz → 1카운트 = 1us
    pwm_set_wrap(slice, 20000);      // 20ms 주기 = 50Hz
    pwm_set_enabled(slice, true);
}

static void servo_angle(uint pin, int deg) {
    if (deg < 0) deg = 0;
    if (deg > 180) deg = 180;
    pwm_set_gpio_level(pin, 500 + deg * 2000 / 180);
}

// ---------- 초음파 ----------
static float measure_cm(void) {
    gpio_put(TRIG_PIN, 1);
    sleep_us(10);
    gpio_put(TRIG_PIN, 0);
    absolute_time_t deadline = make_timeout_time_us(30000);
    while (!gpio_get(ECHO_PIN))
        if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) return -1;
    absolute_time_t start = get_absolute_time();
    while (gpio_get(ECHO_PIN))
        if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) return -2;
    return (float)absolute_time_diff_us(start, get_absolute_time()) / 58.0f;
}

// ---------- LCD1602 (PCF8574 I2C 백팩, 4비트 모드) ----------
static int lcd_addr = 0x27;  // scan으로 실제 주소 확인 (0x27 또는 0x3F)
#define LCD_BL 0x08

// MAP A(P0=RS, P2=EN) · 타이밍 여유판 — lcd_test.uf2로 검증 완료(2026-07-20)
static void lcd_wr(uint8_t b) {
    i2c_write_blocking(i2c0, lcd_addr, &b, 1, false);
}

static void lcd_nib(uint8_t nib, uint8_t rs) {
    uint8_t base = (nib & 0xF0) | LCD_BL | (rs ? 0x01 : 0);
    lcd_wr(base);          sleep_us(50);
    lcd_wr(base | 0x04);   sleep_us(50);    // EN ↑
    lcd_wr(base);          sleep_us(150);   // EN ↓ — 래치
}

static void lcd_byte(uint8_t v, uint8_t rs) {
    lcd_nib(v & 0xF0, rs);
    lcd_nib((v << 4) & 0xF0, rs);
    sleep_us(150);
}

static void lcd_init(void) {
    sleep_ms(60);
    lcd_nib(0x30, 0); sleep_ms(6);
    lcd_nib(0x30, 0); sleep_ms(6);
    lcd_nib(0x30, 0); sleep_ms(6);
    lcd_nib(0x20, 0); sleep_ms(6);   // 4비트 모드
    lcd_byte(0x28, 0); sleep_ms(2);  // 2줄, 5x8
    lcd_byte(0x08, 0); sleep_ms(2);  // 화면 OFF
    lcd_byte(0x01, 0); sleep_ms(5);  // 클리어
    lcd_byte(0x06, 0); sleep_ms(2);
    lcd_byte(0x0C, 0); sleep_ms(2);  // 화면 ON
}

static void lcd_print(int line, const char *s) {
    lcd_byte(line == 0 ? 0x80 : 0xC0, 0);
    for (int i = 0; i < 16; i++)
        lcd_byte(i < (int)strlen(s) ? s[i] : ' ', 1);
}

// ---------- I2C 스캔 ----------
static int i2c_scan(void) {
    int found = -1;
    printf("I2C 스캔:");
    for (int a = 0x08; a < 0x78; a++) {
        uint8_t rx;
        if (i2c_read_timeout_us(i2c0, a, &rx, 1, false, 2000) >= 0) {
            printf(" 0x%02X", a);
            if (a == 0x27 || a == 0x3F) found = a;
        }
    }
    printf(found < 0 ? "  ← 장치 없음! LCD 배선(SDA=GP4,SCL=GP5,전원) 확인\n"
                     : "  ← LCD 발견\n");
    return found;
}

// ---------- 명령 처리 ----------
static void cmd_help(void) {
    printf("명령: help scan dist watch s1 <각도> s2 <각도> sweep lcd <텍스트> lcd2 <텍스트> all\n");
}

static void run_cmd(char *line) {
    if (!strcmp(line, "help")) cmd_help();
    else if (!strcmp(line, "scan")) {
        int a = i2c_scan();
        if (a > 0) { lcd_addr = a; lcd_init(); lcd_print(0, "LCD OK"); printf("LCD 초기화 완료 (주소 0x%02X)\n", a); }
    }
    else if (!strcmp(line, "dist")) {
        float d = measure_cm();
        if (d > 0) printf("거리 %.1fcm\n", d);
        else printf(d == -1 ? "실패: 에코 시작 없음 (센서 전원/TRIG 확인)\n"
                            : "실패: 에코 안 끝남 (반사 없음 — 센서 정면에 물체 대기)\n");
    }
    else if (!strcmp(line, "watch")) {
        for (int i = 0; i < 20; i++) {
            float d = measure_cm();
            if (d > 0) printf("[%2d] %.1fcm\n", i + 1, d);
            else printf("[%2d] 측정 실패\n", i + 1);
            sleep_ms(300);
        }
    }
    else if (!strncmp(line, "s1 ", 3)) { servo_angle(SERVO1_PIN, atoi(line + 3)); printf("서보1 → %d도\n", atoi(line + 3)); }
    else if (!strncmp(line, "s2 ", 3)) { servo_angle(SERVO2_PIN, atoi(line + 3)); printf("서보2 → %d도\n", atoi(line + 3)); }
    else if (!strcmp(line, "sweep")) {
        printf("서보 왕복 (전원 부족하면 여기서 보드가 리셋됨 — 그게 진단 결과)\n");
        for (int a = 0; a <= 180; a += 30) { servo_angle(SERVO1_PIN, a); sleep_ms(200); }
        for (int a = 180; a >= 0; a -= 30) { servo_angle(SERVO1_PIN, a); sleep_ms(200); }
        for (int a = 0; a <= 180; a += 30) { servo_angle(SERVO2_PIN, a); sleep_ms(200); }
        for (int a = 180; a >= 0; a -= 30) { servo_angle(SERVO2_PIN, a); sleep_ms(200); }
        printf("완료\n");
    }
    else if (!strncmp(line, "lcd2 ", 5)) { lcd_print(1, line + 5); printf("LCD 2줄: %s\n", line + 5); }
    else if (!strncmp(line, "lcd ", 4)) { lcd_print(0, line + 4); printf("LCD 1줄: %s\n", line + 4); }
    else if (!strcmp(line, "all")) {
        printf("=== 전 장치 자동 점검 (4종) ===\n");
        // 1) LCD
        int a = i2c_scan();
        if (a > 0) {
            lcd_addr = a; lcd_init();
            lcd_print(0, "HW TEST 4CH"); lcd_print(1, "LCD OK");
            printf("[1] LCD: OK (0x%02X) — 화면 확인\n", a);
        } else printf("[1] LCD: 실패 (I2C 무응답)\n");

        // 2) 초음파 (3회 중 최선)
        float d = -1;
        for (int i = 0; i < 3 && d <= 0; i++) { d = measure_cm(); sleep_ms(80); }
        if (d > 0) {
            char buf[20];
            snprintf(buf, sizeof(buf), "SONAR %5.1fcm", (double)d);
            if (lcd_addr) lcd_print(1, buf);
            printf("[2] 초음파: OK (%.1fcm)\n", (double)d);
        } else printf("[2] 초음파: 실패 (반사 없음 — 센서 정면에 물체)\n");

        // 3) 서보1 / 4) 서보2 — 하나씩 순차 구동 (전류 피크 회피)
        if (lcd_addr) lcd_print(0, "SERVO1 MOVING");
        printf("[3] 서보1 왕복...\n");
        for (int ang = 60; ang <= 120; ang += 20) { servo_angle(SERVO1_PIN, ang); sleep_ms(250); }
        servo_angle(SERVO1_PIN, 90); sleep_ms(300);

        if (lcd_addr) lcd_print(0, "SERVO2 MOVING");
        printf("[4] 서보2 왕복...\n");
        for (int ang = 60; ang <= 120; ang += 20) { servo_angle(SERVO2_PIN, ang); sleep_ms(250); }
        servo_angle(SERVO2_PIN, 90);

        if (lcd_addr) { lcd_print(0, "ALL CHECK DONE"); lcd_print(1, "SEE SERIAL"); }
        printf("서보: 육안 확인 (안 움직이면 건전지 전원·GND 공통 확인)\n=== 점검 끝 ===\n");
    }
    else if (strlen(line)) printf("모르는 명령: %s (help 입력)\n", line);
}

int main(void) {
    stdio_init_all();

    servo_init(SERVO1_PIN);
    servo_init(SERVO2_PIN);
    servo_angle(SERVO1_PIN, 90);
    servo_angle(SERVO2_PIN, 90);

    gpio_init(TRIG_PIN); gpio_set_dir(TRIG_PIN, GPIO_OUT); gpio_put(TRIG_PIN, 0);
    gpio_init(ECHO_PIN); gpio_set_dir(ECHO_PIN, GPIO_IN);

    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);   // 내부 풀업 (백팩에도 있지만 안전빵)
    gpio_pull_up(SCL_PIN);

    sleep_ms(2500);
    printf("\n=== Pico 2 W 통합 하드웨어 테스트 ===\n");
    cmd_help();

    char buf[64];
    int n = 0;
    while (true) {
        int c = getchar_timeout_us(100000);
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (c == '\r' || c == '\n') {
            printf("\n");
            buf[n] = 0;
            run_cmd(buf);
            n = 0;
            printf("> ");
        } else if ((c == 8 || c == 127) && n > 0) {  // 백스페이스
            n--; printf("\b \b");
        } else if (n < 63 && c >= 32) {
            buf[n++] = (char)c;
            putchar(c);  // 에코 (PuTTY에 입력이 보이게)
        }
    }
}
