#!/usr/bin/env python3
"""노트북 실시간 스트림 판정 — 브라우저 웹캠 → WSL 모델 서버.

WSL은 USB 웹캠에 직접 접근할 수 없으므로, 브라우저(Windows)가 웹캠을 잡고
프레임을 이 서버로 보내면 Run B-17 + 채택 후처리로 판정해 돌려준다.

실행:   python3 laptop_stream.py
접속:   Windows 브라우저에서  http://localhost:8090
"""
import io
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import cv2
import numpy as np
from ultralytics import YOLO

MODEL = Path(__file__).resolve().parent.parent / "models/runB17_yolo11n_17cls_best.pt"
PORT = 8090
CONF_TH = 0.40
DIRTY_TH = 0.70
IGNORE_DIRTY = True                      # 축소 운용안과 동일
BIN_ITEMS = ["paper", "plastic", "vinyl"]

GROUP = {
    "pet_transparent": ("plastic", False), "pet_dirty": ("plastic", True),
    "plastic": ("plastic", False),     "plastic_dirty": ("plastic", True),
    "can": ("can", False),             "can_dirty": ("can", True),
    "glass": ("glass", False),         "glass_dirty": ("glass", True),
    "paper": ("paper", False),         "paper_dirty": ("paper", True),
    "paper_pack": ("paper", False), "paper_pack_dirty": ("paper", True),
    "vinyl": ("vinyl", False),         "vinyl_dirty": ("vinyl", True),
    "styrofoam": ("styrofoam", False), "styrofoam_dirty": ("styrofoam", True),
    "battery": ("battery", False),
}
BINS = set(BIN_ITEMS)
EN = {"pet": "PET", "plastic": "PLASTIC", "can": "CAN", "glass": "GLASS",
      "paper": "PAPER", "paper_pack": "PAPER PACK", "vinyl": "VINYL",
      "styrofoam": "STYROFOAM", "battery": "BATTERY"}

model = YOLO(str(MODEL))


def iou(a, b):
    x1, y1 = max(a[0], b[0]), max(a[1], b[1])
    x2, y2 = min(a[2], b[2]), min(a[3], b[3])
    inter = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    ar = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter
    return inter / (ar + 1e-9)


def merge_overlaps(raw, iou_th=0.55):
    """겹친 박스 = 같은 물체 → 최고 신뢰도 1개만 (한 물체 다중판정 방지)"""
    kept = []
    for name, conf, box in sorted(raw, key=lambda d: -d[1]):
        if not any(iou(k[2], box) >= iou_th for k in kept):
            kept.append((name, conf, box))
    return kept


def judge(dets):
    """→ (표시문구, 색상BGR, Pico명령)"""
    strong = [d for d in dets if d[1] >= CONF_TH]
    if not dets:
        return "NO OBJECT", (160, 160, 160), None
    if len(strong) >= 2 and len({GROUP[n][0] for n, _, _ in strong}) >= 2:
        return "MULTIPLE -> RETURN", (0, 140, 255), "REJECT MULTI"
    name, conf, _ = (strong or dets)[0]
    group, dirty = GROUP[name]
    if conf < CONF_TH:
        return "LOW CONF %.0f%% -> RETURN" % (conf * 100), (0, 140, 255), "REJECT LOWCONF"
    if group in BINS:
        if not IGNORE_DIRTY and dirty and conf >= DIRTY_TH:
            return "DIRTY %s -> WASH" % EN[group], (0, 140, 255), "REJECT DIRTY"
        return "%s %.0f%% -> BIN" % (EN[group], conf * 100), (0, 190, 0), "SORT " + group.upper()
    return "%s %.0f%% -> RETURN (no bin)" % (EN[group], conf * 100), (0, 140, 255), "REJECT OTHER"


def infer_jpeg(jpg_bytes):
    img = cv2.imdecode(np.frombuffer(jpg_bytes, np.uint8), cv2.IMREAD_COLOR)
    if img is None:
        return b""
    t0 = time.time()
    r = model.predict(img, conf=0.25, imgsz=640, verbose=False)[0]
    raw = [(model.names[int(b.cls)], float(b.conf), b.xyxy[0].tolist()) for b in r.boxes]
    dets = merge_overlaps(raw)
    dt = time.time() - t0

    for n, c, b in dets:
        x1, y1, x2, y2 = map(int, b)
        col = (0, 140, 255) if GROUP[n][1] else (0, 190, 0)
        cv2.rectangle(img, (x1, y1), (x2, y2), col, 2)
        cv2.putText(img, "%s %.2f" % (n, c), (x1, max(16, y1 - 6)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, col, 2)

    text, col, cmd = judge(dets)
    cv2.rectangle(img, (0, 0), (img.shape[1], 46), (0, 0, 0), -1)
    cv2.putText(img, text, (10, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.85, col, 2)
    info = "%.0f ms" % (dt * 1000) + ("   [%s]" % cmd if cmd else "")
    cv2.putText(img, info, (img.shape[1] - 320, 32),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (200, 200, 200), 2)

    ok, out = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 82])
    return out.tobytes() if ok else b""


PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>Laptop YOLO Stream</title>
<style>body{background:#111;color:#eee;font-family:sans-serif;text-align:center;margin:0;padding:10px}
img,video{max-width:96%;border:1px solid #333}#src{display:none}</style></head><body>
<h3>Recycling Sorter - Laptop Live (Run B-17)</h3>
<video id="src" autoplay playsinline></video>
<img id="out">
<p id="st">camera starting...</p>
<script>
const v=document.getElementById('src'),o=document.getElementById('out'),st=document.getElementById('st');
const cv=document.createElement('canvas');
async function startCam(onready){
  try{
    let s=await navigator.mediaDevices.getUserMedia({video:true});
    const devs=await navigator.mediaDevices.enumerateDevices();
    const c920=devs.find(d=>d.kind==='videoinput'&&/C920/i.test(d.label));
    if(c920){
      s.getTracks().forEach(t=>t.stop());
      s=await navigator.mediaDevices.getUserMedia(
        {video:{deviceId:{exact:c920.deviceId},width:1280,height:720}});
    }
    v.srcObject=s;
    onready(c920?'C920 카메라 사용':'C920 미발견 — 기본 카메라 사용 (연결 확인!)');
  }catch(e){onready('카메라 오류: '+e);}
}
startCam(m=>{st.textContent=m;loop();});
let busy=false, t0=0;
async function loop(){
  if(!busy && v.videoWidth){
    busy=true; t0=performance.now();
    cv.width=v.videoWidth; cv.height=v.videoHeight;
    cv.getContext('2d').drawImage(v,0,0);
    cv.toBlob(async b=>{
      try{
        const r=await fetch('/infer',{method:'POST',body:b});
        const blob=await r.blob();
        o.src=URL.createObjectURL(blob);
        st.textContent=((performance.now()-t0)|0)+' ms/frame';
      }catch(e){st.textContent='server error: '+e;}
      busy=false;
    },'image/jpeg',0.85);
  }
  setTimeout(loop,60);
}
</script></body></html>"""


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = PAGE.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        jpg = infer_jpeg(self.rfile.read(n))
        self.send_response(200)
        self.send_header("Content-Type", "image/jpeg")
        self.send_header("Content-Length", str(len(jpg)))
        self.end_headers()
        self.wfile.write(jpg)

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    print("모델 로드 완료 — Windows 브라우저에서 http://localhost:%d 접속" % PORT)
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
