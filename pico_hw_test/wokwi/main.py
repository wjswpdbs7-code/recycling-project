# Pico 통합 하드웨어 테스트 — Wokwi 시뮬레이션용 (MicroPython)
# 실물 C 펌웨어(pico_hw_test/main.c)의 축약판: 시작하면 자동으로
# I2C 스캔 → LCD 주소 확인 → 서보 왕복 → 초음파 연속 측정을 순서대로 시연.
# (실물은 시리얼 명령 방식이지만, 시뮬레이션은 자동 시연으로 배선 검증)
from machine import Pin, PWM, I2C, time_pulse_us
import time

# ---- 서보 (GP0, GP1) ----
servo1 = PWM(Pin(0)); servo1.freq(50)
servo2 = PWM(Pin(1)); servo2.freq(50)

def angle(servo, deg):
    us = 500 + int(deg * 2000 / 180)
    servo.duty_u16(int(us * 65535 / 20000))

# ---- 초음파 (GP16 TRIG, GP17 ECHO) ----
TRIG = Pin(16, Pin.OUT, value=0)
ECHO = Pin(17, Pin.IN)

def dist_cm():
    TRIG.value(1); time.sleep_us(10); TRIG.value(0)
    us = time_pulse_us(ECHO, 1, 30000)
    return us / 58.0 if us > 0 else -1

# ---- I2C (GP4 SDA, GP5 SCL) ----
i2c = I2C(0, sda=Pin(4), scl=Pin(5), freq=100000)

print("=== 통합 하드웨어 시뮬레이션 ===")

# 1) I2C 스캔 — LCD 백팩이 0x27로 잡혀야 정상
found = i2c.scan()
print("I2C 스캔:", [hex(a) for a in found] or "장치 없음(배선 확인)")

# 2) 서보 왕복
print("서보1·2 왕복...")
for d in (0, 90, 180, 90):
    angle(servo1, d); angle(servo2, 180 - d)
    time.sleep_ms(600)

# 3) 초음파 연속 측정 — HC-SR04 클릭 후 거리 슬라이더로 조작
print("초음파 측정 시작 (센서 클릭 → 슬라이더로 거리 바꿔보기)")
while True:
    d = dist_cm()
    print("거리 %.1fcm" % d if d > 0 else "측정 실패")
    time.sleep_ms(500)
