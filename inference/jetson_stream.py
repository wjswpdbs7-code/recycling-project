#!/usr/bin/env python3
"""Jetson 실시간 스트리밍 판정 — 브라우저에서 YOLO 탐지를 눈으로 확인.

Jetson에서 실행:  python3 jetson_stream.py
노트북 브라우저:  http://192.168.55.1:8080

C920 프레임을 계속 읽어 TensorRT로 판정하고, 박스·판정문을 그려 MJPEG로 송출한다.
(라벨은 OpenCV 한글 미지원이라 영문 표기)
"""
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

import cv2
import numpy as np

from jetson_infer import Engine, preprocess, postprocess, GROUP, BINS, CONF_TH, DIRTY_TH, ENGINE

PORT = 8080
CAM = 0

# 영문 표기 (OpenCV 한글 렌더 불가)
EN = {"pet": "PET", "plastic": "PLASTIC", "can": "CAN", "glass": "GLASS",
      "paper": "PAPER", "paper_pack": "PAPER PACK", "vinyl": "VINYL",
      "styrofoam": "STYROFOAM", "battery": "BATTERY"}


def judge_en(dets):
    """판정 → (표시문구, 색상BGR)"""
    strong = [d for d in dets if d[1] >= CONF_TH]
    if not dets:
        return "NO OBJECT", (150, 150, 150)
    if len(strong) >= 2 and len({GROUP[n][0] for n, _, _ in strong}) >= 2:
        return "MULTIPLE -> RETURN", (0, 140, 255)

    name, conf, _ = (strong or dets)[0]
    group, _, is_dirty = GROUP[name]
    if conf < CONF_TH:
        return "LOW CONF %.0f%% -> RETURN" % (conf * 100), (0, 140, 255)
    if group in BINS:
        if is_dirty and conf >= DIRTY_TH:
            return "DIRTY %s -> WASH" % EN[group], (0, 140, 255)
        return "%s %.0f%% -> BIN" % (EN[group], conf * 100), (0, 200, 0)
    return "%s %.0f%% -> COACH" % (EN[group], conf * 100), (255, 160, 0)


class Cam:
    def __init__(self, eng):
        self.eng = eng
        self.cap = cv2.VideoCapture(CAM)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
        self.fps = 0.0

    def frame(self):
        ok, img = self.cap.read()
        if not ok:
            return None
        t0 = time.time()
        x, r, dx, dy = preprocess(img)
        out = self.eng.infer(x)
        dets = postprocess(out, r, dx, dy, img.shape[:2])
        dt = time.time() - t0
        self.fps = 0.8 * self.fps + 0.2 * (1.0 / max(dt, 1e-3))

        for n, c, b in dets:
            if c < 0.25:
                continue
            x1, y1, x2, y2 = map(int, b)
            grp, _, dirty = GROUP[n]
            col = (0, 200, 0) if not dirty else (0, 140, 255)
            cv2.rectangle(img, (x1, y1), (x2, y2), col, 2)
            cv2.putText(img, "%s %.2f" % (n, c), (x1, max(16, y1 - 6)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.55, col, 2)

        text, col = judge_en(dets)
        cv2.rectangle(img, (0, 0), (img.shape[1], 44), (0, 0, 0), -1)
        cv2.putText(img, text, (10, 31), cv2.FONT_HERSHEY_SIMPLEX, 0.85, col, 2)
        cv2.putText(img, "%.1f FPS  %.0f ms" % (self.fps, dt * 1000),
                    (img.shape[1] - 210, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (200, 200, 200), 2)
        return img


class Handler(BaseHTTPRequestHandler):
    cam = None

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            html = b"""<html><head><title>Jetson YOLO</title>
<style>body{background:#111;color:#eee;font-family:sans-serif;text-align:center;margin:0;padding:12px}
img{max-width:98%;border:1px solid #333}</style></head>
<body><h3>Recycling Sorter - Live Detection (Run B-17 / TensorRT FP16)</h3>
<img src="/stream"></body></html>"""
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(html)))
            self.end_headers()
            self.wfile.write(html)
            return

        if self.path != "/stream":
            self.send_error(404)
            return

        self.send_response(200)
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
        self.end_headers()
        try:
            while True:
                img = Handler.cam.frame()
                if img is None:
                    time.sleep(0.1)
                    continue
                ok, jpg = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 80])
                if not ok:
                    continue
                self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ")
                self.wfile.write(str(len(jpg)).encode())
                self.wfile.write(b"\r\n\r\n")
                self.wfile.write(jpg.tobytes())
                self.wfile.write(b"\r\n")
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *a):
        pass


def main():
    print("TensorRT 엔진 로드 중...")
    eng = Engine(ENGINE)
    Handler.cam = Cam(eng)
    print("스트리밍 시작 → 노트북 브라우저에서 http://192.168.55.1:%d 접속" % PORT)
    HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
