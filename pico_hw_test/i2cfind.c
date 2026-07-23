// I2C 장치 전핀 탐색 — LCD가 어느 핀에 꽂혀 있든 찾아낸다.
// Pico의 모든 I2C 가능 핀쌍을 차례로 시도 (서보 GP0/1, 초음파 GP16/17은 제외).
// 출력: FOUND SDA=<핀> SCL=<핀> ADDR=<주소>  또는  NOT_FOUND
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

typedef struct { i2c_inst_t *inst; uint sda; uint scl; } pair_t;

// i2c0: SDA=0,4,8,12,16,20 SCL=1,5,9,13,17,21 / i2c1: SDA=2,6,10,14,18,26 SCL=3,7,11,15,19,27
static const pair_t PAIRS[] = {
    {i2c0, 4, 5}, {i2c0, 8, 9}, {i2c0, 12, 13}, {i2c0, 20, 21},
    {i2c1, 2, 3}, {i2c1, 6, 7}, {i2c1, 10, 11}, {i2c1, 14, 15}, {i2c1, 18, 19}, {i2c1, 26, 27},
    // 배선이 뒤바뀐 경우도 시도 (SDA/SCL 스왑)
    {i2c0, 5, 4}, {i2c1, 3, 2}, {i2c1, 7, 6},
};
#define NPAIRS (sizeof(PAIRS) / sizeof(PAIRS[0]))

int main(void) {
    stdio_init_all();
    sleep_ms(2500);
    printf("\nI2C_FIND_START\n");

    while (true) {
        int total = 0;
        for (unsigned i = 0; i < NPAIRS; i++) {
            const pair_t *p = &PAIRS[i];
            i2c_init(p->inst, 100 * 1000);
            gpio_set_function(p->sda, GPIO_FUNC_I2C);
            gpio_set_function(p->scl, GPIO_FUNC_I2C);
            gpio_pull_up(p->sda);
            gpio_pull_up(p->scl);
            sleep_ms(5);

            for (int a = 0x08; a < 0x78; a++) {
                uint8_t rx;
                if (i2c_read_timeout_us(p->inst, a, &rx, 1, false, 1500) >= 0) {
                    printf("FOUND SDA=GP%u SCL=GP%u ADDR=0x%02X\n", p->sda, p->scl, a);
                    total++;
                }
            }
            // 다음 조합을 위해 핀 해제
            gpio_set_function(p->sda, GPIO_FUNC_SIO);
            gpio_set_function(p->scl, GPIO_FUNC_SIO);
            gpio_disable_pulls(p->sda);
            gpio_disable_pulls(p->scl);
            i2c_deinit(p->inst);
        }
        printf(total ? "SCAN_DONE found=%d\n" : "NOT_FOUND (LCD 백팩 없음 또는 전원/GND 미연결)\n", total);
        sleep_ms(3000);
    }
}
