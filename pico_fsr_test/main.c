// 전체 하드웨어 테스트 — FSR 8채널(투입 감지) + 서보3 + LCD
//
// FSR이 초음파를 대체한다: 쓰레기가 패드를 누르면 → "DETECT" → 카메라 촬영 트리거.
//
// ── FSR 배선 (외부 저항 불필요!) ──
//   각 채널 한쪽 핀 → GPIO (아래 표), 반대쪽/공통 핀 → GND
//   Pico 내부 풀업 사용: 안 누름 = HIGH, 누름 = LOW (FSR 저항이 낮아져 끌어내림)
//
//   FSR ch1 → GP6  (물리핀 9)     FSR ch5 → GP10 (물리핀 14)
//   FSR ch2 → GP7  (물리핀 10)    FSR ch6 → GP11 (물리핀 15)
//   FSR ch3 → GP8  (물리핀 11)    FSR ch7 → GP12 (물리핀 16)
//   FSR ch4 → GP9  (물리핀 12)    FSR ch8 → GP13 (물리핀 17)
//   FSR 공통(있으면) → GND
//
// ── 기존 배선 유지 ──
//   서보: 왼문=GP0, 회전판=GP1, 오른문=GP2 (전원 별도 5~6V, GND 공통)
//   LCD: SDA=GP4, SCL=GP5
//
// ── 시리얼 명령 (115200) ──
//   fsr           8채널 상태 1회 (누른 채널 표시)
//   watch         연속 관찰 20회 (밟아보면서 확인)
//   all           전 장치 자동 점검 (FSR→LCD→서보 순)
//   open [양] / close      투입구 양문
//   d1|d2|d3 <각도>        서보 개별 (d2=회전판)
//   sort pet|can|plastic|etc   분배 시퀀스 (회전판→양문)
//   lcd <문구>    LCD 표시
//   help
//
// ── 자동 동작 ──
//   FSR이 눌리면(디바운스 0.15초) → "DETECT ch=N" 출력 + LCD "DETECTED"
//   떼면 → "CLEAR" 출력. 상위(Jetson)는 DETECT 줄을 촬영 트리거로 사용.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/i2c.h"

#define DOOR_L_PIN   0
#define CAROUSEL_PIN 1
#define DOOR_R_PIN   2
#define SDA_PIN      4
#define SCL_PIN      5

#define NUM_FSR 1                        // 사용 채널 수 (1개: GP6만)
static const uint FSR_PIN[NUM_FSR] = {6};
#define DEBOUNCE_MS 150

// 서보 설정 (sorter와 동일 구조)
static int cfg_close_l = 0, cfg_close_r = 0, cfg_amount = 60;
static int cfg_bin[4] = {0, 60, 120, 180};
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
static void doors_move(int tl, int tr) {
    tl = clamp(tl); tr = clamp(tr);
    while (cur_l != tl || cur_r != tr) {
        if (cur_l != tl) { cur_l += (tl > cur_l) ? 1 : -1; servo_raw(DOOR_L_PIN, cur_l); }
        if (cur_r != tr) { cur_r += (tr > cur_r) ? 1 : -1; servo_raw(DOOR_R_PIN, cur_r); }
        sleep_ms(8);
    }
    sleep_ms(120);
}
static void carousel_move(int t) {
    t = clamp(t);
    while (cur_car != t) {
        int s = (t > cur_car) ? 2 : -2;
        cur_car += s;
        if ((s > 0 && cur_car > t) || (s < 0 && cur_car < t)) cur_car = t;
        servo_raw(CAROUSEL_PIN, cur_car);
        sleep_ms(24);
    }
    sleep_ms(300);
}

// ── LCD (MAP A, 검증 타이밍) ──
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

// ── FSR ──
static uint8_t fsr_read(void) {           // 비트마스크: 눌린 채널 = 1
    uint8_t m = 0;
    for (int i = 0; i < NUM_FSR; i++)
        if (!gpio_get(FSR_PIN[i])) m |= (1 << i);   // LOW = 눌림
    return m;
}
static void fsr_print(uint8_t m) {
    printf("FSR(GP6) [%c] %s\n", m ? '#' : '.', m ? "눌림!" : "(무압력)");
}

static void do_sort(int bin) {
    char b[20];
    snprintf(b, sizeof(b), "-> %s", BIN_NAME[bin]);
    lcd_show("SORTING", b);
    printf("분배: %s (회전판 %d도)\n", BIN_NAME[bin], cfg_bin[bin]);
    carousel_move(cfg_bin[bin]);
    doors_move(cfg_close_l + cfg_amount, cfg_close_r - cfg_amount >= 0 ? cfg_close_r - cfg_amount : cfg_close_r + cfg_amount);
    sleep_ms(900);
    doors_move(cfg_close_l, cfg_close_r);
    lcd_show("DONE", b);
    printf("DONE %s\n", BIN_NAME[bin]);
}

static void run_cmd(char *line) {
    for (char *p = line; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;  // 소문자화

    if (!strcmp(line, "help")) {
        printf("명령: fsr / watch / all / open [양] / close / d1|d2|d3 <각> / sort <칸> / lcd <문구> / help\n");
    }
    else if (!strcmp(line, "fsr")) fsr_print(fsr_read());
    else if (!strcmp(line, "watch")) {
        printf("연속 관찰 20회 — 패드를 밟아보세요\n");
        for (int i = 0; i < 20; i++) { fsr_print(fsr_read()); sleep_ms(400); }
        printf("관찰 끝\n");
    }
    else if (!strcmp(line, "all")) {
        printf("=== 전 장치 자동 점검 ===\n");
        // 1) FSR
        uint8_t m = fsr_read();
        printf("[1] FSR: "); fsr_print(m);
        printf("    (모든 채널 '.'이면 정상 대기 — 눌러서 '#' 바뀌는지 watch로 확인)\n");
        // 2) LCD
        if (lcd_addr) { lcd_show("HW TEST", "FSR+SERVO+LCD"); printf("[2] LCD: OK (0x%02X)\n", lcd_addr); }
        else printf("[2] LCD: 미검출\n");
        // 3) 서보 순차 꿈틀
        printf("[3] 왼문(GP0) 꿈틀...\n");
        doors_move(cur_l + 25, cur_r); doors_move(cfg_close_l, cur_r);
        printf("[4] 회전판(GP1) 꿈틀...\n");
        carousel_move(30); carousel_move(0);
        printf("[5] 오른문(GP2) 꿈틀...\n");
        doors_move(cur_l, cur_r + 25); doors_move(cur_l, cfg_close_r);
        printf("=== 점검 끝 (서보는 육안 확인) ===\n");
        if (lcd_addr) lcd_show("ALL CHECK DONE", "");
    }
    else if (!strncmp(line, "open", 4)) {
        int amt = (line[4] == ' ') ? atoi(line + 5) : cfg_amount;
        printf("양문 열기 %d\n", amt);
        doors_move(cfg_close_l + amt, cfg_close_r + amt);  // 거울 장착 기준 (+/+)
        printf("완료\n");
    }
    else if (!strcmp(line, "close")) { doors_move(cfg_close_l, cfg_close_r); printf("양문 닫힘\n"); }
    else if (!strncmp(line, "d1 ", 3)) { doors_move(atoi(line + 3), cur_r); printf("왼문=%d\n", cur_l); }
    else if (!strncmp(line, "d2 ", 3)) { carousel_move(atoi(line + 3)); printf("회전판=%d\n", cur_car); }
    else if (!strncmp(line, "d3 ", 3)) { doors_move(cur_l, atoi(line + 3)); printf("오른문=%d\n", cur_r); }
    else if (!strncmp(line, "sort ", 5)) {
        for (int i = 0; i < 4; i++) {
            char nm[12];
            strcpy(nm, BIN_NAME[i]);
            for (char *p = nm; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
            if (!strcmp(line + 5, nm)) { do_sort(i); return; }
        }
        printf("칸: pet can plastic etc\n");
    }
    else if (!strncmp(line, "lcd ", 4)) { lcd_print(0, line + 4); printf("LCD 표시: %s\n", line + 4); }
    else if (strlen(line)) printf("모르는 명령: %s (help)\n", line);
}

int main(void) {
    stdio_init_all();

    servo_init(DOOR_L_PIN); servo_init(CAROUSEL_PIN); servo_init(DOOR_R_PIN);
    cur_l = cfg_close_l; cur_r = cfg_close_r; cur_car = cfg_bin[0];
    servo_raw(DOOR_L_PIN, cur_l);
    servo_raw(CAROUSEL_PIN, cur_car);
    servo_raw(DOOR_R_PIN, cur_r);

    for (int i = 0; i < NUM_FSR; i++) {     // FSR: 내부 풀업 입력
        gpio_init(FSR_PIN[i]);
        gpio_set_dir(FSR_PIN[i], GPIO_IN);
        gpio_pull_up(FSR_PIN[i]);
    }

    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN); gpio_pull_up(SCL_PIN);

    sleep_ms(1500);
    for (int a = 0x08; a < 0x78 && !lcd_addr; a++) {
        uint8_t rx;
        if (i2c_read_timeout_us(i2c0, a, &rx, 1, false, 2000) >= 0) lcd_addr = a;
    }
    if (lcd_addr) { lcd_init(); lcd_show("FSR SORTER", "STEP TO DETECT"); }

    sleep_ms(1000);
    printf("\n=== 전체 HW 테스트: FSR(GP6) + 서보3 + LCD (0x%02X) ===\n", lcd_addr);
    printf("FSR: GP6 1채널, 반대쪽=GND (내부 풀업, 누르면 감지)\n");
    run_cmd("help");

    char buf[64];
    int n = 0;
    bool detected = false;
    absolute_time_t press_t = get_absolute_time();
    absolute_time_t next_scan = get_absolute_time();

    while (true) {
        // 시리얼 명령 (논블로킹 + 에코)
        int c = getchar_timeout_us(0);
        while (c != PICO_ERROR_TIMEOUT) {
            if (c == '\r' || c == '\n') {
                buf[n] = 0;
                if (n) { printf("\n"); run_cmd(buf); printf("> "); }
                n = 0;
            } else if ((c == 8 || c == 127) && n > 0) {
                n--; printf("\b \b");
            } else if (n < 63 && c >= 32) {
                buf[n++] = (char)c;
                putchar(c);
            }
            c = getchar_timeout_us(0);
        }

        // FSR 감시 (50ms 주기 + 디바운스)
        if (absolute_time_diff_us(get_absolute_time(), next_scan) < 0) {
            next_scan = make_timeout_time_ms(50);
            uint8_t m = fsr_read();
            if (m && !detected) {
                if (absolute_time_diff_us(press_t, get_absolute_time()) > DEBOUNCE_MS * 1000) {
                    detected = true;
                    printf("DETECT\n");
                    lcd_show("DETECTED", "ANALYZING...");
                }
            } else if (!m) {
                press_t = get_absolute_time();
                if (detected) {
                    detected = false;
                    printf("CLEAR\n");
                    lcd_show("FSR SORTER", "STEP TO DETECT");
                }
            }
        }
    }
}
