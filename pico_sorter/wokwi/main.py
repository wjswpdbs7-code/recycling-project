# 분류기 구동부 — Wokwi 시뮬레이션용 (MicroPython)
# 실물 C 펌웨어(pico_sorter/main.c)와 동일한 핀·각도·시퀀스.
# 시작하면 자동 시연: I2C 스캔 → 4칸 순회(슈트+셔터) → 초음파 투입 감지 루프
from machine import Pin, PWM, I2C, time_pulse_us
import time

CHUTE, SHUTTER = PWM(Pin(0)), PWM(Pin(1))
CHUTE.freq(50); SHUTTER.freq(50)
TRIG = Pin(16, Pin.OUT, value=0)
ECHO = Pin(17, Pin.IN)
i2c = I2C(0, sda=Pin(4), scl=Pin(5), freq=100000)

BIN_ANGLE = (0, 60, 120, 180)
BIN_NAME = ("PET", "CAN", "PLASTIC", "ETC")
SHUTTER_CLOSED, SHUTTER_OPEN = 10, 100
DETECT_CM, CLEAR_CM = 15.0, 18.0

def angle(servo, deg):
    servo.duty_u16(int((500 + deg * 2000 // 180) * 65535 / 20000))

_chute = 0
def chute_move(target):            # 3도씩 천천히 (전류 스파이크 방지)
    global _chute
    step = 3 if target > _chute else -3
    while _chute != target:
        _chute += step
        if (step > 0 and _chute > target) or (step < 0 and _chute < target):
            _chute = target
        angle(CHUTE, _chute)
        time.sleep_ms(20)
    time.sleep_ms(250)

def dist_cm():
    TRIG.value(1); time.sleep_us(10); TRIG.value(0)
    us = time_pulse_us(ECHO, 1, 30000)
    return us / 58.0 if us > 0 else -1

def do_sort(i):
    print("분배:", BIN_NAME[i], BIN_ANGLE[i], "도")
    chute_move(BIN_ANGLE[i])
    angle(SHUTTER, SHUTTER_OPEN);   time.sleep_ms(900)
    angle(SHUTTER, SHUTTER_CLOSED); time.sleep_ms(400)
    print("DONE", BIN_NAME[i])

angle(CHUTE, 0); angle(SHUTTER, SHUTTER_CLOSED)
print("=== 분류기 구동부 (시뮬레이션) ===")
print("I2C 스캔:", [hex(a) for a in i2c.scan()] or "장치 없음")

for i in range(4):                 # 자가 점검: 4칸 순회
    do_sort(i)
chute_move(0)
print("자가 점검 완료 — 초음파 감지 대기\n")

detected = False
while True:                        # 투입 감지 루프 (HC-SR04 클릭 → 슬라이더 조작)
    d = dist_cm()
    if d > 0:
        if d < DETECT_CM and not detected:
            detected = True
            print("DETECT %.1f" % d)
        elif d >= CLEAR_CM and detected:
            detected = False
            print("CLEAR %.1f" % d)
    time.sleep_ms(200)
