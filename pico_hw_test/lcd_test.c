// LCD 글자 깨짐 진단 — 핀 매핑 2종을 번갈아 시도 (각 6초)
//
// PCF8574 백팩은 제조사에 따라 두 가지 배선이 있다:
//   MAP A (표준/다수): P0=RS P1=RW P2=EN P3=BL  D4~D7=P4~P7
//   MAP B (일부 제품): P0=EN P1=RW P2=RS P3=BL  D4~D7=P4~P7
// 화면에 "MAP A 0123456789" 가 또렷하게 보이는 쪽이 정답.
// 타이밍도 여유 있게(150us) 잡아 초기화 실패 가능성을 함께 제거.
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define SDA_PIN 4
#define SCL_PIN 5
#define BL 0x08

static int addr = 0;
static int map_mode = 0;   // 0 = MAP A, 1 = MAP B

// 매핑에 따라 RS/EN 비트 위치가 달라진다
static uint8_t bit_rs(void) { return map_mode == 0 ? 0x01 : 0x04; }
static uint8_t bit_en(void) { return map_mode == 0 ? 0x04 : 0x01; }

static void wr(uint8_t b) {
    i2c_write_blocking(i2c0, addr, &b, 1, false);
}

static void nib(uint8_t n, uint8_t rs) {
    uint8_t base = (n & 0xF0) | BL | (rs ? bit_rs() : 0);
    wr(base);
    sleep_us(50);
    wr(base | bit_en());     // EN ↑
    sleep_us(50);
    wr(base);                // EN ↓ — 이 순간 데이터가 래치됨
    sleep_us(150);
}

static void byte(uint8_t v, uint8_t rs) {
    nib(v & 0xF0, rs);
    nib((v << 4) & 0xF0, rs);
    sleep_us(150);
}

static void init_lcd(void) {
    sleep_ms(60);
    nib(0x30, 0); sleep_ms(6);    // 8비트 모드 3회 (규격상 필수)
    nib(0x30, 0); sleep_ms(6);
    nib(0x30, 0); sleep_ms(6);
    nib(0x20, 0); sleep_ms(6);    // 4비트 모드 전환
    byte(0x28, 0); sleep_ms(2);   // 2줄, 5x8 폰트
    byte(0x08, 0); sleep_ms(2);   // 화면 OFF
    byte(0x01, 0); sleep_ms(5);   // 클리어 (오래 걸림)
    byte(0x06, 0); sleep_ms(2);   // 진행 방향
    byte(0x0C, 0); sleep_ms(2);   // 화면 ON, 커서 OFF
}

static void put(int line, const char *s) {
    byte(line == 0 ? 0x80 : 0xC0, 0);
    for (int i = 0; i < 16; i++) byte(i < (int)strlen(s) ? s[i] : ' ', 1);
}

int main(void) {
    stdio_init_all();
    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    sleep_ms(2000);
    printf("\n=== LCD 매핑 진단 ===\n");

    for (int a = 0x08; a < 0x78 && !addr; a++) {
        uint8_t rx;
        if (i2c_read_timeout_us(i2c0, a, &rx, 1, false, 2000) >= 0) addr = a;
    }
    if (!addr) { printf("I2C 응답 없음\n"); while (1) tight_loop_contents(); }
    printf("LCD 주소 0x%02X\n", addr);

    while (true) {
        for (map_mode = 0; map_mode < 2; map_mode++) {
            printf("지금 화면: MAP %c — 또렷하면 이게 정답\n", map_mode == 0 ? 'A' : 'B');
            init_lcd();
            put(0, map_mode == 0 ? "MAP A 12345678" : "MAP B 12345678");
            put(1, "ABCDEFGHIJKLMNOP");
            sleep_ms(6000);
        }
    }
}
