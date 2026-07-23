// 분류기 구동부 — 3칸 자동 사이클 (초음파 + 서보2 + LCD)
//
// ── 분배 체계 (2026-07-23 확정) ──
//   회전판 0° = 종이(PAPER) / 90° = 플라스틱(PLASTIC) / 180° = 비닐(VINYL)
//   그 외 품목·판별불가 = REJECT (투입구 안 열음, 사용자가 회수)
//
// ── 자동 사이클 (상태머신) ──
//   [대기] 초음파 감시 중
//     ↓ 물체 감지 (15cm 미만)
//   [안정화] 3초 대기 (손 빼는 시간) — LCD "WAIT 3 SEC"
//     ↓ 3초 경과 (도중 치우면 대기로 복귀)
//   "DETECT" 송신 → [분석대기] ★초음파 무시★ — 상위가 촬영·분석
//     ↓ "SORT xxx" 수신
//   회전판 정렬 → 투입구 열기 → 닫기 → "DONE" 송신
//     ↓ 2초 정착
//   [대기]로 복귀 (여기서부터 초음파 다시 인지)
//     ※ "REJECT" 수신 시: 문 안 열고 안내 → 물체 치워질 때까지 대기 후 재무장
//
// ── 배선 ──
//   투입구 서보 = 물리핀 1 (GP0), 닫힘 90° / 열림 180° (실측 확정)
//   회전판 서보 = 물리핀 2 (GP1), 0/90/180°
//   HC-SR04: TRIG=GP16, ECHO=GP17(1k/2k 분압), VCC=5V
//   LCD1602 I2C: SDA=GP4, SCL=GP5
//
// ── 시리얼 명령 ──
//   SORT PAPER|PLASTIC|VINYL / REJECT [사유]   ← 상위(분석기)가 보냄
//   dist / watch / all / open [양] / close / d1|d2 <각> / status / help  ← 수동
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/i2c.h"

#define DOOR_PIN     0    // 투입구 = 물리핀 1
#define CAROUSEL_PIN 1    // 회전판 = 물리핀 2
#define TRIG_PIN    16
#define ECHO_PIN    17
#define SDA_PIN      4
#define SCL_PIN      5

#define DETECT_CM   15.0f
#define CLEAR_CM    18.0f
#define SETTLE_MS   3000   // 감지 후 촬영까지 대기 (손 빼는 시간)
#define COOLDOWN_MS 2000   // 분배 후 재무장까지 대기

// ── 실측 캘리브레이션 (2026-07-22/23 확정) ──
static int cfg_door_close = 90;
static int cfg_door_open  = 180;
static int cfg_bin[3] = {0, 90, 180};
static const char *BIN_NAME[3] = {"PAPER", "PLASTIC", "VINYL"};

static int cur_door, cur_car;
static char last_result[17] = "PUT ONE ITEM";   // 대기화면 2줄째: 마지막 판정 결과
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
static void door_move(int t) {
    t = clamp(t);
    while (cur_door != t) {
        cur_door += (t > cur_door) ? 1 : -1;
        servo_raw(DOOR_PIN, cur_door);
        sleep_ms(8);
    }
    sleep_ms(150);
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
    sleep_ms(400);   // 봉투 흔들림 정착
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
static float median3f(float a, float b, float c) {
    if ((a >= b && a <= c) || (a <= b && a >= c)) return a;
    if ((b >= a && b <= c) || (b <= a && b >= c)) return b;
    return c;
}
static float sonar_cm(void) { return median3f(measure_cm(), measure_cm(), measure_cm()); }

// ── 상태머신 ──
typedef enum { ST_ARMED, ST_SETTLING, ST_WAITCMD, ST_WAITCLEAR, ST_COOLDOWN } state_t;
static state_t state = ST_ARMED;
static absolute_time_t t_state;

static void enter(state_t s) {
    state = s;
    t_state = get_absolute_time();
}

static void do_sort(int bin) {
    char b[20];
    // 판별 결과 표시 (예: "RESULT: PAPER")
    snprintf(b, sizeof(b), "RESULT: %s", BIN_NAME[bin]);
    lcd_show(b, "SORTING...");
    printf("분배: %s (회전판 %d도)\n", BIN_NAME[bin], cfg_bin[bin]);
    carousel_move(cfg_bin[bin]);      // 1) 회전판 정렬
    door_move(cfg_door_open);         // 2) 투입구 열기
    sleep_ms(900);                    // 3) 낙하 대기
    door_move(cfg_door_close);        // 4) 투입구 닫기
    snprintf(b, sizeof(b), "%s SORTED", BIN_NAME[bin]);
    lcd_show("COMPLETE!", b);
    snprintf(last_result, sizeof(last_result), "LAST: %s", BIN_NAME[bin]);
    printf("DONE %s\n", BIN_NAME[bin]);
    enter(ST_COOLDOWN);               // 사이클 종료 → 정착 후 재무장
}

static void run_cmd(char *line) {
    for (char *p = line; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;

    if (!strncmp(line, "sort ", 5)) {
        for (int i = 0; i < 3; i++) {
            char nm[10]; int k = 0;
            for (const char *q = BIN_NAME[i]; *q; q++) nm[k++] = *q + 32;
            nm[k] = 0;
            if (!strcmp(line + 5, nm)) { do_sort(i); return; }
        }
        printf("칸: paper plastic vinyl\n");
    }
    else if (!strncmp(line, "reject", 6)) {
        // 사유별 안내: REJECT OTHER / MULTI / LOWCONF
        const char *why = line + 6;
        while (*why == ' ') why++;
        if (!strcmp(why, "multi"))        lcd_show("ONE ITEM ONLY!", "TAKE IT BACK");
        else if (!strcmp(why, "lowconf")) lcd_show("UNKNOWN ITEM", "TAKE IT BACK");
        else if (!strcmp(why, "other"))   lcd_show("NO BIN FOR THIS", "TAKE IT BACK");
        else                              lcd_show("NOT SORTABLE", "TAKE IT BACK");
        snprintf(last_result, sizeof(last_result), "LAST: RETURNED");
        printf("리턴 안내(%s)\n", *why ? why : "-");
        enter(ST_COOLDOWN);           // 대기 없이 쿨다운 후 재무장
    }
    else if (!strcmp(line, "status")) {
        const char *names[] = {"ARMED", "SETTLING", "WAITCMD", "WAITCLEAR", "COOLDOWN"};
        printf("상태=%s 문=%d 회전판=%d 거리=%.1fcm\n", names[state], cur_door, cur_car, sonar_cm());
    }
    else if (!strcmp(line, "dist")) { float d = sonar_cm(); printf(d > 0 ? "거리 %.1fcm\n" : "측정 실패\n", d); }
    else if (!strcmp(line, "watch")) {
        for (int i = 0; i < 20; i++) { float d = sonar_cm(); printf(d > 0 ? "거리 %.1fcm\n" : "측정 실패\n", d); sleep_ms(400); }
    }
    else if (!strcmp(line, "all")) {
        printf("=== 점검 ===\n");
        float d = sonar_cm();
        printf("[1] 초음파: %s (%.1fcm)\n", d > 0 ? "OK" : "실패", d);
        printf("[2] LCD: %s\n", lcd_addr ? "OK" : "미검출");
        printf("[3] 회전판 꿈틀...\n"); carousel_move(30); carousel_move(0);
        printf("[4] 투입구 꿈틀...\n"); door_move(cfg_door_close + 25); door_move(cfg_door_close);
        printf("=== 끝 ===\n");
    }
    else if (!strncmp(line, "open", 4)) {
        int amt = (line[4] == ' ') ? atoi(line + 5) : (cfg_door_open - cfg_door_close);
        door_move(cfg_door_close + amt); printf("투입구 %d도\n", cur_door);
    }
    else if (!strcmp(line, "close")) { door_move(cfg_door_close); printf("투입구 닫힘\n"); }
    else if (!strncmp(line, "d1 ", 3)) { door_move(atoi(line + 3)); printf("투입구=%d\n", cur_door); }
    else if (!strncmp(line, "d2 ", 3)) { carousel_move(atoi(line + 3)); printf("회전판=%d\n", cur_car); }
    else if (!strcmp(line, "help")) {
        printf("자동: SORT PAPER|PLASTIC|VINYL / REJECT\n");
        printf("수동: dist watch all open close d1|d2 <각> status help\n");
    }
    else if (strlen(line)) printf("모르는 명령: %s\n", line);
}

int main(void) {
    stdio_init_all();

    servo_init(DOOR_PIN); servo_init(CAROUSEL_PIN);
    cur_door = cfg_door_close; cur_car = cfg_bin[0];
    servo_raw(DOOR_PIN, cur_door);
    servo_raw(CAROUSEL_PIN, cur_car);

    gpio_init(TRIG_PIN); gpio_set_dir(TRIG_PIN, GPIO_OUT); gpio_put(TRIG_PIN, 0);
    gpio_init(ECHO_PIN); gpio_set_dir(ECHO_PIN, GPIO_IN);

    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN); gpio_pull_up(SCL_PIN);

    sleep_ms(1500);
    for (int a = 0x08; a < 0x78 && !lcd_addr; a++) {
        uint8_t rx;
        if (i2c_read_timeout_us(i2c0, a, &rx, 1, false, 2000) >= 0) lcd_addr = a;
    }
    if (lcd_addr) { lcd_init(); lcd_show("SORTER READY", last_result); }

    sleep_ms(1000);
    printf("\n=== 분류기 3칸 자동사이클 (LCD 0x%02X) ===\n", lcd_addr);
    printf("칸: 0도=PAPER / 90도=PLASTIC / 180도=VINYL | 문: 닫힘90/열림180\n");
    enter(ST_ARMED);

    char buf[64];
    int n = 0;
    absolute_time_t next_scan = get_absolute_time();

    while (true) {
        // 시리얼 명령 (논블로킹 + 에코)
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
                putchar(c);
            }
            c = getchar_timeout_us(0);
        }

        // 상태머신 (0.15초 주기)
        if (absolute_time_diff_us(get_absolute_time(), next_scan) > 0) continue;
        next_scan = make_timeout_time_ms(150);
        int64_t elapsed = absolute_time_diff_us(t_state, get_absolute_time()) / 1000;

        switch (state) {
        case ST_ARMED: {
            if (elapsed > 30000) {                 // 30초마다 화면 자가복구
                if (lcd_addr) lcd_init();
                lcd_show("SORTER READY", last_result);
                t_state = get_absolute_time();
            }
            float d = sonar_cm();
            if (d > 0 && d < DETECT_CM) {
                lcd_show("ITEM DETECTED", "WAIT 3 SEC...");
                printf("감지 — 3초 안정화 대기\n");
                enter(ST_SETTLING);
            }
            break;
        }
        case ST_SETTLING:
            if (elapsed >= SETTLE_MS) {            // 3초 경과 → 무조건 촬영 트리거
                printf("DETECT\n");
                lcd_show("ANALYZING...", "PLEASE WAIT");
                enter(ST_WAITCMD);                 // ★ 이후 초음파 무시
            }
            break;
        case ST_WAITCMD:
            // 초음파 무시하고 상위 명령(SORT/REJECT)만 기다림.
            if (elapsed > 30000) {                 // 30초 무응답 → 안전 복귀
                printf("분석 응답 없음 — 대기로 복귀\n");
                lcd_show("SORTER READY", last_result);
                enter(ST_ARMED);
            }
            break;
        case ST_WAITCLEAR:                         // (미사용 — 즉시 재무장)
            if (lcd_addr) lcd_init();
            lcd_show("SORTER READY", last_result);
            enter(ST_ARMED);
            break;
        case ST_COOLDOWN:                          // 분배 후 정착
            if (elapsed >= COOLDOWN_MS) {
                if (lcd_addr) lcd_init();          // 서보 노이즈로 깨졌을 수 있어 재초기화
                lcd_show("SORTER READY", last_result);
                enter(ST_ARMED);                   // ★ 여기서부터 초음파 재인지
            }
            break;
        }
    }
}
