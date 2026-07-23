# HC-SR04 초음파 투입 감지 — Wokwi 시뮬레이션용 (MicroPython)
# 실물 C 펌웨어(hcsr04_test/main.c)와 동일한 로직:
#   3회 측정 중앙값 → 10cm 진입 시 DETECT 1회 / 12cm 이탈 시 CLEAR (히스테리시스)
from machine import Pin, time_pulse_us
import time

TRIG = Pin(16, Pin.OUT, value=0)
ECHO = Pin(17, Pin.IN)
DETECT_CM = 10.0
CLEAR_CM = DETECT_CM + 2   # 히스테리시스: 경계 떨림 방지
TIMEOUT_US = 30000

def measure_cm():
    TRIG.value(1)
    time.sleep_us(10)
    TRIG.value(0)
    us = time_pulse_us(ECHO, 1, TIMEOUT_US)  # ECHO HIGH 폭 측정 (실패 시 음수)
    return us / 58.0 if us > 0 else -1.0

def median3(a, b, c):
    return sorted((a, b, c))[1]

print("HC-SR04 테스트 시작 (임계 %.0fcm)" % DETECT_CM)
detected = False
while True:
    a, b, c = measure_cm(), measure_cm(), measure_cm()
    if a < 0 or b < 0 or c < 0:
        print("측정 실패 (에코 없음 — 배선 확인)")
    else:
        d = median3(a, b, c)
        if d < DETECT_CM and not detected:
            detected = True
            print("DETECT %.1fcm" % d)   # ← 투입 감지 이벤트 (진입 순간 1회)
        elif d >= CLEAR_CM and detected:
            detected = False
            print("CLEAR %.1fcm" % d)
        else:
            print("거리 %.1fcm" % d)
    time.sleep_ms(200)
