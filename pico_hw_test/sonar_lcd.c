// 초음파 거리 → LCD 실시간 표시 (투입 감지 데모)
//
// 배선: HC-SR04 TRIG=GP16, ECHO=GP17(1k/2k 분압)
//       LCD I2C  SDA=GP4, SCL=GP5, VCC=5V, GND 공통
//
// LCD 1줄: "DIST:  12.3cm"   (실시간 거리)
// LCD 2줄: "WAITING" / "** DETECTED **"  (10cm 이내 감지 시)
// 시리얼에도 같은 내용 출력.
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define TRIG_PIN 16
#define ECHO_PIN 17
#define SDA_PIN 4
#define SCL_PIN 5
#define DETECT_CM 10.0f
#define CLEAR_CM 12.0f

static int lcd_addr = 0;
#define LCD_BL 0x08

// 매핑 MAP A(P0=RS, P2=EN) 확정 · 타이밍 여유 확보판 (lcd_test로 검증)
static void lcd_wr(uint8_t b) {
    i2c_write_blocking(i2c0, lcd_addr, &b, 1, false);
}
static void lcd_nib(uint8_t nib, uint8_t rs) {
    uint8_t base = (nib & 0xF0) | LCD_BL | (rs ? 0x01 : 0);
    lcd_wr(base);            sleep_us(50);
    lcd_wr(base | 0x04);     sleep_us(50);   // EN ↑
    lcd_wr(base);            sleep_us(150);  // EN ↓ — 여기서 래치
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

int main(void) {
    stdio_init_all();

    gpio_init(TRIG_PIN); gpio_set_dir(TRIG_PIN, GPIO_OUT); gpio_put(TRIG_PIN, 0);
    gpio_init(ECHO_PIN); gpio_set_dir(ECHO_PIN, GPIO_IN);

    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    sleep_ms(2000);
    printf("\n=== 초음파 → LCD 데모 ===\n");

    // LCD 주소 자동 탐지 (0x27 / 0x3F 등)
    for (int a = 0x08; a < 0x78 && !lcd_addr; a++) {
        uint8_t rx;
        if (i2c_read_timeout_us(i2c0, a, &rx, 1, false, 2000) >= 0) lcd_addr = a;
    }
    if (!lcd_addr) {
        printf("LCD 없음 — I2C 응답 없음. 시리얼로만 출력합니다.\n");
    } else {
        printf("LCD 발견: 0x%02X\n", lcd_addr);
        lcd_init();
        lcd_print(0, "SONAR READY");
        lcd_print(1, "FINAL PROJECT");
        sleep_ms(1200);
    }

    bool detected = false;
    char l1[20], l2[20];
    while (true) {
        float d = median3(measure_cm(), measure_cm(), measure_cm());

        if (d < 0) {
            snprintf(l1, sizeof(l1), "DIST:  --.-cm");
            snprintf(l2, sizeof(l2), "NO ECHO");
        } else {
            snprintf(l1, sizeof(l1), "DIST: %5.1fcm", (double)d);
            if (d < DETECT_CM && !detected) detected = true;
            else if (d >= CLEAR_CM && detected) detected = false;
            snprintf(l2, sizeof(l2), detected ? "** DETECTED **" : "WAITING...");
        }

        if (lcd_addr) { lcd_print(0, l1); lcd_print(1, l2); }
        printf("%s | %s\n", l1, l2);
        sleep_ms(300);
    }
}
