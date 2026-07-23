// 배선 자동 검사 펌웨어 — 결과를 ASCII 코드로 출력 (원격 판독용)
// 2초마다 전 항목을 재검사하므로, 배선을 고치면 즉시 결과가 바뀐다.
//
// 출력 형식 (한 줄에 하나, 판독하기 쉬운 KEY=VALUE):
//   ECHO_PULLUP=0|1   0=분압 연결됨(정상) / 1=GP17 미연결
//   ECHO_IDLE=0|1     0=정상 / 1=배선오류
//   SONAR=OK <cm> | NO_RISE | NO_FALL
//   I2C=<주소들> | NONE
//   SERVO=MOVED (육안 확인 필요)
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/i2c.h"

#define SERVO1_PIN 0
#define SERVO2_PIN 1
#define TRIG_PIN 16
#define ECHO_PIN 17
#define SDA_PIN 4
#define SCL_PIN 5

static void servo_init(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(slice, 150.0f);
    pwm_set_wrap(slice, 20000);
    pwm_set_enabled(slice, true);
}
static void servo_angle(uint pin, int deg) {
    pwm_set_gpio_level(pin, 500 + deg * 2000 / 180);
}

int main(void) {
    stdio_init_all();

    servo_init(SERVO1_PIN);
    servo_init(SERVO2_PIN);
    gpio_init(TRIG_PIN); gpio_set_dir(TRIG_PIN, GPIO_OUT); gpio_put(TRIG_PIN, 0);
    gpio_init(ECHO_PIN); gpio_set_dir(ECHO_PIN, GPIO_IN);
    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    sleep_ms(2500);
    printf("\nWIRING_CHECK_START\n");

    int round = 0;
    while (true) {
        printf("--- ROUND %d ---\n", ++round);

        // 1) GP17에 분압회로가 붙어 있는지 (내부 풀업 후 레벨)
        gpio_pull_up(ECHO_PIN);
        sleep_ms(5);
        int up = gpio_get(ECHO_PIN);
        gpio_disable_pulls(ECHO_PIN);
        sleep_ms(5);
        printf("ECHO_PULLUP=%d\n", up);

        // 2) 평상시 레벨
        printf("ECHO_IDLE=%d\n", gpio_get(ECHO_PIN));

        // 3) 초음파 왕복 (3회 시도 중 최선)
        const char *res = "NO_RISE";
        float cm = 0;
        for (int t = 0; t < 3; t++) {
            gpio_put(TRIG_PIN, 1); sleep_us(10); gpio_put(TRIG_PIN, 0);
            absolute_time_t t0 = get_absolute_time();
            int64_t rise = -1, fall = -1;
            while (absolute_time_diff_us(t0, get_absolute_time()) < 40000)
                if (gpio_get(ECHO_PIN)) { rise = absolute_time_diff_us(t0, get_absolute_time()); break; }
            if (rise >= 0) {
                while (absolute_time_diff_us(t0, get_absolute_time()) < 40000)
                    if (!gpio_get(ECHO_PIN)) { fall = absolute_time_diff_us(t0, get_absolute_time()); break; }
                if (fall >= 0) { res = "OK"; cm = (float)(fall - rise) / 58.0f; break; }
                res = "NO_FALL";
            }
            sleep_ms(60);
        }
        if (res[0] == 'O') printf("SONAR=OK %.1f\n", cm);
        else printf("SONAR=%s\n", res);

        // 4) I2C 스캔 (LCD)
        printf("I2C=");
        int n = 0;
        for (int a = 0x08; a < 0x78; a++) {
            uint8_t rx;
            if (i2c_read_timeout_us(i2c0, a, &rx, 1, false, 2000) >= 0) { printf("%02X ", a); n++; }
        }
        printf(n ? "\n" : "NONE\n");

        // 5) 서보 꿈틀 (육안 확인용, 소각도라 전류 부담 적음)
        servo_angle(SERVO1_PIN, 75); servo_angle(SERVO2_PIN, 105);
        sleep_ms(400);
        servo_angle(SERVO1_PIN, 105); servo_angle(SERVO2_PIN, 75);
        sleep_ms(400);
        servo_angle(SERVO1_PIN, 90); servo_angle(SERVO2_PIN, 90);
        printf("SERVO=WIGGLED\n");

        sleep_ms(2000);
    }
}
