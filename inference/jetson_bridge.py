#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Jetson 전자동 사이클 브리지 + 노트북 모니터링 (Python 3.6 호환).

역할 (노트북 브리지의 Jetson판 — 브라우저 없이 전부 이 스크립트가 수행):
  Pico "DETECT" 수신(/dev/ttyACM0) → C920 10프레임/1초 촬영 →
  TensorRT 판정 → 다수결 → "SORT/REJECT" 송신 → 기구 동작

노트북에서 보기:
  크롬에서  http://192.168.55.1:8081  → 실시간 판정 화면(MJPEG) + 최근 로그

실행(Jetson):  python3 jetson_bridge.py
"""
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, HTTPServer
import socketserver

import cv2
import serial

from jetson_infer import Engine, preprocess, postprocess, ENGINE

PORT = 8081
SERIAL_DEV = "/dev/ttyACM0"
CONF_TH = 0.40

# ── 3칸 체계 (0=종이 / 90=플라스틱 / 180=비닐, 그 외 리턴) ──
GROUP = {
    "pet_transparent": "plastic", "pet_dirty": "plastic",
    "plastic": "plastic", "plastic_dirty": "plastic",
    "paper": "paper", "paper_dirty": "paper",
    "paper_pack": "paper", "paper_pack_dirty": "paper",
    "vinyl": "vinyl", "vinyl_dirty": "vinyl",
    "can": "can", "can_dirty": "can",
    "glass": "glass", "glass_dirty": "glass",
    "styrofoam": "styrofoam", "styrofoam_dirty": "styrofoam",
    "battery": "battery",
}
BINS = {"paper", "plastic", "vinyl"}

latest_jpeg = None          # MJPEG 서버가 내보낼 최신 프레임
logs = deque(maxlen=40)     # 웹페이지에 보여줄 최근 로그
lock = threading.Lock()


def log(msg):
    line = "[%s] %s" % (time.strftime("%H:%M:%S"), msg)
    print(line)
    logs.append(line)


def judge_frame(eng, img):
    """1프레임 → (명령, 설명, dets)"""
    x, r, dx, dy = preprocess(img)
    out = eng.infer(x)
    dets = postprocess(out, r, dx, dy, img.shape[:2])

    # 겹침 병합 (한 물체 다중 판정 방지)
    merged = []
    for name, conf, box in sorted(dets, key=lambda d: -d[1]):
        dup = False
        for mn, mc, mb in merged:
            x1 = max(box[0], mb[0]); y1 = max(box[1], mb[1])
            x2 = min(box[2], mb[2]); y2 = min(box[3], mb[3])
            inter = max(0.0, x2 - x1) * max(0.0, y2 - y1)
            ar = ((box[2]-box[0])*(box[3]-box[1]) + (mb[2]-mb[0])*(mb[3]-mb[1]) - inter)
            if inter / (ar + 1e-9) >= 0.55:
                dup = True
                break
        if not dup:
            merged.append((name, conf, box))
    dets = merged
    strong = [d for d in dets if d[1] >= CONF_TH]

    if not dets:
        return "NONE", "no object", dets
    if len(strong) >= 2 and len(set(GROUP[n] for n, _, _ in strong)) >= 2:
        return "REJECT MULTI", "multiple items", dets
    name, conf, _ = (strong or dets)[0]
    g = GROUP[name]
    if conf < CONF_TH:
        return "REJECT LOWCONF", "%s %.0f%%" % (name, conf * 100), dets
    if g in BINS:
        return "SORT " + g.upper(), "%s %.0f%%" % (name, conf * 100), dets
    return "REJECT OTHER", "%s %.0f%%" % (name, conf * 100), dets


def publish(img, dets, header):
    global latest_jpeg
    vis = img.copy()
    for n, c, b in dets:
        x1, y1, x2, y2 = [int(v) for v in b]
        cv2.rectangle(vis, (x1, y1), (x2, y2), (0, 190, 0), 2)
        cv2.putText(vis, "%s %.2f" % (n, c), (x1, max(16, y1 - 6)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 190, 0), 2)
    cv2.rectangle(vis, (0, 0), (vis.shape[1], 42), (0, 0, 0), -1)
    cv2.putText(vis, header, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 220, 255), 2)
    ok, jp = cv2.imencode(".jpg", vis, [cv2.IMWRITE_JPEG_QUALITY, 78])
    if ok:
        with lock:
            latest_jpeg = jp.tobytes()


# ── 웹 모니터 (MJPEG + 로그) ──
class ThreadedHTTPServer(socketserver.ThreadingMixIn, HTTPServer):
    daemon_threads = True


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/stream":
            self.send_response(200)
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
            self.end_headers()
            try:
                while True:
                    with lock:
                        jp = latest_jpeg
                    if jp:
                        self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\n\r\n")
                        self.wfile.write(jp)
                        self.wfile.write(b"\r\n")
                    time.sleep(0.12)
            except Exception:
                pass
            return
        if self.path == "/log":
            body = "\n".join(logs).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        html = ("<html><head><meta charset='utf-8'><title>Jetson Sorter</title>"
                "<style>body{background:#111;color:#eee;font-family:sans-serif;"
                "text-align:center;margin:0;padding:10px}img{max-width:96%%;border:1px solid #333}"
                "pre{background:#000;border:1px solid #333;text-align:left;font-size:12px;"
                "height:170px;overflow-y:auto;padding:6px}</style>"
                "<script>setInterval(async()=>{const r=await fetch('/log');"
                "const t=await r.text();const p=document.getElementById('lg');"
                "p.textContent=t;p.scrollTop=p.scrollHeight;},1000)</script></head>"
                "<body><h3>Jetson Auto Sorter - Live</h3>"
                "<img src='/stream'><pre id='lg'></pre></body></html>").encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(html)))
        self.end_headers()
        self.wfile.write(html)

    def log_message(self, *a):
        pass


def open_camera():
    # 카메라가 늦게 인식되거나(부팅 직후/케이블 재삽입) 아직 없을 때 재시도
    for attempt in range(30):
        cap = cv2.VideoCapture(0)
        if cap.isOpened():
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
            cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
            ok, _ = cap.read()
            if ok:
                if attempt:
                    log("카메라 연결 복구 (%d회 재시도)" % attempt)
                return cap
        cap.release()
        if attempt == 0:
            log("카메라 열기 실패 — 5초 간격 재시도(최대 30회)")
        time.sleep(5)
    return None


def main():
    log("TensorRT 엔진 로드 중...")
    eng = Engine(ENGINE)

    cap = open_camera()
    if cap is None:
        log("카메라 포기 — 종료(systemd가 재시작)")
        raise SystemExit(1)

    ser = None
    for attempt in range(10):          # 재시도 (권한/재열거 일시 오류 대응)
        try:
            ser = serial.Serial(SERIAL_DEV, 115200, timeout=0.1)
            break
        except Exception as e:
            log("시리얼 열기 실패(%d/10): %s" % (attempt + 1, e))
            time.sleep(2)
    if ser is None:
        log("시리얼 포기 — 카메라/웹만 동작")
    else:
        log("시리얼 연결: %s" % SERIAL_DEV)

    srv = ThreadedHTTPServer(("0.0.0.0", PORT), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    log("모니터링: 노트북에서 http://192.168.55.1:%d" % PORT)

    last_live = 0.0
    cam_fail = 0
    buf = b""
    while True:
        # 시리얼 수신
        data = ser.read(256) if ser else b""
        if data:
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                try:
                    text = line.decode("utf-8", "ignore").strip()
                except Exception:
                    text = ""
                if not text:
                    continue
                log("[Pico] " + text)

                if text.startswith("DETECT"):
                    # ── 사이클: 10프레임/1초 → 다수결 ──
                    frames = []
                    for i in range(10):
                        ok, f = cap.read()
                        if ok:
                            frames.append(f)
                        time.sleep(0.1)
                    votes = {}
                    for i, f in enumerate(frames):
                        cmd, desc, dets = judge_frame(eng, f)
                        votes[cmd] = votes.get(cmd, 0) + 1
                        publish(f, dets, "ANALYZE %d/10: %s (%s)" % (i + 1, cmd, desc))
                        log("  frame%d: %s (%s)" % (i + 1, cmd, desc))
                    best, bn = None, -1
                    for c, n in votes.items():
                        if c == "NONE":
                            continue
                        if n > bn:
                            best, bn = c, n
                    if not best:
                        best = "REJECT LOWCONF"
                    log("★ 다수결: %s (%d/10표)" % (best, max(bn, 0)))
                    if ser:
                        ser.write((best + "\r\n").encode())

        # 대기 중 실시간 미리보기 (0.4초마다)
        now = time.time()
        if now - last_live >= 0.4:
            last_live = now
            ok, f = cap.read()
            if ok:
                cam_fail = 0
                cmd, desc, dets = judge_frame(eng, f)
                publish(f, dets, "LIVE: %s (%s)" % (cmd, desc))
            else:
                cam_fail += 1
                if cam_fail >= 15:      # 약 6초 연속 실패 → 카메라 재연결 시도
                    log("카메라 응답 없음 — 재연결 시도")
                    cap.release()
                    cap = open_camera()
                    if cap is None:
                        log("카메라 포기 — 종료(systemd가 재시작)")
                        raise SystemExit(1)
                    cam_fail = 0


if __name__ == "__main__":
    main()
