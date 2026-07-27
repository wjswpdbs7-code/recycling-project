# 분류기 구동부 — Wokwi 시뮬레이션용 (MicroPython)
# 실물 C 펌웨어(pico_sonar_test/main.c, 최종 설계 2026-07-23 확정)와 동일한 핀·각도·상태머신.
#   서보1 투입구 도어 GP0 (닫힘 90 / 열림 180)
#   서보2 회전판   GP1 (0=PAPER / 90=PLASTIC / 180=VINYL)
#   HC-SR04 TRIG=GP16, ECHO=GP17(1k/2k 분압) · LCD1602 I2C0 SDA=GP4/SCL=GP5
#   실물은 Pico 2 W + Jetson USB 시리얼 — 시뮬레이터에선 시리얼 모니터에 직접 입력:
#   SORT PAPER|PLASTIC|VINYL / REJECT [multi|lowconf|other] / status / dist / open / close
import select
import sys
import time
from machine import I2C, PWM, Pin, time_pulse_us

DOOR, CAROUSEL = PWM(Pin(0)), PWM(Pin(1))
DOOR.freq(50)
CAROUSEL.freq(50)
TRIG = Pin(16, Pin.OUT, value=0)
ECHO = Pin(17, Pin.IN)
i2c = I2C(0, sda=Pin(4), scl=Pin(5), freq=100000)

DOOR_CLOSE, DOOR_OPEN = 90, 180
BIN_ANGLE = (0, 90, 180)
BIN_NAME = ("PAPER", "PLASTIC", "VINYL")
DETECT_CM, CLEAR_CM = 15.0, 18.0
SETTLE_MS, COOLDOWN_MS = 3000, 2000


def servo_raw(servo, deg):
    deg = max(0, min(180, deg))
    servo.duty_u16(int((500 + deg * 2000 // 180) * 65535 / 20000))


cur_door, cur_car = DOOR_CLOSE, BIN_ANGLE[0]
servo_raw(DOOR, cur_door)
servo_raw(CAROUSEL, cur_car)


def door_move(target):                 # 1도/8ms — 실물과 동일 속도
    global cur_door
    step = 1 if target > cur_door else -1
    while cur_door != target:
        cur_door += step
        servo_raw(DOOR, cur_door)
        time.sleep_ms(8)
    time.sleep_ms(150)


def carousel_move(target):             # 2도/24ms — 수거함 무게 배려
    global cur_car
    while cur_car != target:
        step = 2 if target > cur_car else -2
        cur_car += step
        if (step > 0 and cur_car > target) or (step < 0 and cur_car < target):
            cur_car = target
        servo_raw(CAROUSEL, cur_car)
        time.sleep_ms(24)
    time.sleep_ms(400)                 # 봉투 흔들림 정착


# ── LCD1602 (PCF8574 백팩, MAP A) ──
lcd_addr = 0
BL = 0x08


def lcd_wr(b):
    i2c.writeto(lcd_addr, bytes([b]))


def lcd_nib(nib, rs):
    base = (nib & 0xF0) | BL | (0x01 if rs else 0)
    lcd_wr(base)
    lcd_wr(base | 0x04)
    lcd_wr(base)
    time.sleep_us(150)


def lcd_byte(v, rs):
    lcd_nib(v & 0xF0, rs)
    lcd_nib((v << 4) & 0xF0, rs)


def lcd_init():
    time.sleep_ms(60)
    for _ in range(3):
        lcd_nib(0x30, 0)
        time.sleep_ms(6)
    lcd_nib(0x20, 0)
    time.sleep_ms(6)
    for cmd in (0x28, 0x08, 0x01, 0x06, 0x0C):
        lcd_byte(cmd, 0)
        time.sleep_ms(5)


def lcd_show(a, b=""):
    if not lcd_addr:
        return
    for line, s in ((0x80, a), (0xC0, b)):
        lcd_byte(line, 0)
        for i in range(16):
            lcd_byte(ord(s[i]) if i < len(s) else 32, 1)


found = i2c.scan()
if found:
    lcd_addr = found[0]
    lcd_init()

# ── 초음파 (3회 중앙값) ──


def measure_cm():
    TRIG.value(1)
    time.sleep_us(10)
    TRIG.value(0)
    us = time_pulse_us(ECHO, 1, 30000)
    return -1 if us < 0 else us / 58.0


def sonar_cm():
    v = sorted(measure_cm() for _ in range(3))
    return v[1]


# ── 상태머신 ──
ARMED, SETTLING, WAITCMD, COOLDOWN = range(4)
state = ARMED
t_state = time.ticks_ms()
last_result = "PUT ONE ITEM"


def enter(s):
    global state, t_state
    state = s
    t_state = time.ticks_ms()


def do_sort(i):
    global last_result
    lcd_show("RESULT: " + BIN_NAME[i], "SORTING...")
    print("분배: %s (회전판 %d도)" % (BIN_NAME[i], BIN_ANGLE[i]))
    carousel_move(BIN_ANGLE[i])        # 1) 회전판 정렬 (문 열기 전에!)
    door_move(DOOR_OPEN)               # 2) 투입구 열기
    time.sleep_ms(900)                 # 3) 낙하 대기
    door_move(DOOR_CLOSE)              # 4) 투입구 닫기
    lcd_show("COMPLETE!", BIN_NAME[i] + " SORTED")
    last_result = "LAST: " + BIN_NAME[i]
    print("DONE " + BIN_NAME[i])
    enter(COOLDOWN)


def run_cmd(line):
    global last_result
    line = line.strip().lower()
    if line.startswith("sort "):
        w = line[5:]
        for i, nm in enumerate(BIN_NAME):
            if w == nm.lower():
                do_sort(i)
                return
        print("칸: paper plastic vinyl")
    elif line.startswith("reject"):
        why = line[6:].strip()
        msg = {"multi": "ONE ITEM ONLY!", "lowconf": "UNKNOWN ITEM",
               "other": "NO BIN FOR THIS"}.get(why, "NOT SORTABLE")
        lcd_show(msg, "TAKE IT BACK")
        last_result = "LAST: RETURNED"
        print("리턴 안내(%s)" % (why or "-"))
        enter(COOLDOWN)
    elif line == "status":
        print("상태=%d 문=%d 회전판=%d 거리=%.1fcm" % (state, cur_door, cur_car, sonar_cm()))
    elif line == "dist":
        print("거리 %.1fcm" % sonar_cm())
    elif line.startswith("open"):
        door_move(DOOR_OPEN)
        print("투입구 %d도" % cur_door)
    elif line == "close":
        door_move(DOOR_CLOSE)
        print("투입구 닫힘")
    elif line.startswith("d1 "):
        door_move(int(line[3:]))
    elif line.startswith("d2 "):
        carousel_move(int(line[3:]))
    elif line == "help":
        print("자동: SORT PAPER|PLASTIC|VINYL / REJECT [사유]")
        print("수동: dist status open close d1|d2 <각> help")
    elif line:
        print("모르는 명령: " + line)


print("=== 분류기 3칸 자동사이클 (LCD 0x%02X) ===" % lcd_addr)
print("칸: 0도=PAPER / 90도=PLASTIC / 180도=VINYL | 문: 닫힘90/열림180")
lcd_show("SORTER READY", last_result)

poller = select.poll()
poller.register(sys.stdin, select.POLLIN)
buf = ""

while True:
    # 시리얼 명령 (논블로킹) — 실물에선 Jetson이 보냄
    while poller.poll(0):
        ch = sys.stdin.read(1)
        if ch in ("\r", "\n"):
            if buf:
                run_cmd(buf)
            buf = ""
        else:
            buf += ch

    time.sleep_ms(150)
    elapsed = time.ticks_diff(time.ticks_ms(), t_state)

    if state == ARMED:
        if elapsed > 30000:            # 30초마다 화면 자가복구 (서보 노이즈 대책)
            if lcd_addr:
                lcd_init()
            lcd_show("SORTER READY", last_result)
            t_state = time.ticks_ms()
        d = sonar_cm()
        if 0 < d < DETECT_CM:
            lcd_show("ITEM DETECTED", "WAIT 3 SEC...")
            print("감지 — 3초 안정화 대기")
            enter(SETTLING)
    elif state == SETTLING:
        if elapsed >= SETTLE_MS:       # 3초 경과 → 촬영 트리거
            print("DETECT")
            lcd_show("ANALYZING...", "PLEASE WAIT")
            enter(WAITCMD)             # ★ 이후 초음파 무시
    elif state == WAITCMD:
        if elapsed > 30000:            # 30초 무응답 → 안전 복귀
            print("분석 응답 없음 — 대기로 복귀")
            lcd_show("SORTER READY", last_result)
            enter(ARMED)
    elif state == COOLDOWN:
        if elapsed >= COOLDOWN_MS:
            lcd_show("SORTER READY", last_result)
            enter(ARMED)
