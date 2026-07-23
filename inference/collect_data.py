#!/usr/bin/env python3
"""fine-tuning 데이터 수집·반자동 라벨링 도구.

브라우저에서 웹캠 미리보기를 보며 숫자키를 누르면:
  프레임 저장 → 기존 Run B-17 모델이 물체 박스를 찾음(위치) →
  클래스는 누른 숫자로 기록(사람) → YOLO 형식 라벨 완성

실행:  python3 collect_data.py
접속:  http://localhost:8091   (크롬)

키 배정 (17클래스 학습 인덱스와 일치):
  1 투명페트  2 플라스틱  3 캔  4 유리  5 종이
  6 종이팩   7 비닐     8 스티로폼  9 건전지
  0 네거티브(비대상 물체/빈 스테이지 — 박스 없는 배경 이미지로 저장)

저장 위치: training/dataset_finetune/{images,labels}/
파일명: {클래스}_{순번}.jpg / .txt
"""
import base64
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

import cv2
import numpy as np
from ultralytics import YOLO

ROOT = Path(__file__).resolve().parent.parent
MODEL = ROOT / "models/runB17_yolo11n_17cls_best.pt"
OUT = ROOT / "training/dataset_finetune"
PORT = 8091

# 키 → (17클래스 인덱스, 이름). 0 = 네거티브
KEYMAP = {
    "1": (0, "pet_transparent"), "2": (1, "plastic"), "3": (2, "can"),
    "4": (3, "glass"), "5": (4, "paper"), "6": (5, "paper_pack"),
    "7": (6, "vinyl"), "8": (7, "styrofoam"), "9": (8, "battery"),
    "0": (-1, "negative"),
}

(OUT / "images").mkdir(parents=True, exist_ok=True)
(OUT / "labels").mkdir(parents=True, exist_ok=True)

model = YOLO(str(MODEL))
recent = []   # 최근 저장 stem 스택 (실행취소용)


def counts():
    c = {}
    for f in (OUT / "images").glob("*.jpg"):
        k = f.stem.rsplit("_", 1)[0]
        c[k] = c.get(k, 0) + 1
    return c


def next_index(name):
    n = 0
    for f in (OUT / "images").glob(f"{name}_*.jpg"):
        try:
            n = max(n, int(f.stem.rsplit("_", 1)[1]) + 1)
        except ValueError:
            pass
    return n


def save_sample(jpg_bytes, key):
    cls_idx, name = KEYMAP[key]
    img = cv2.imdecode(np.frombuffer(jpg_bytes, np.uint8), cv2.IMREAD_COLOR)
    if img is None:
        return {"ok": False, "msg": "이미지 디코드 실패"}
    h, w = img.shape[:2]

    idx = next_index(name)
    stem = f"{name}_{idx:04d}"
    cv2.imwrite(str(OUT / "images" / f"{stem}.jpg"), img)
    recent.append(stem)

    def preview(im, box=None, color=(0, 190, 0), label=""):
        p = im.copy()
        if box:
            x1, y1, x2, y2 = map(int, box)
            cv2.rectangle(p, (x1, y1), (x2, y2), color, 3)
            cv2.putText(p, label, (x1, max(22, y1 - 8)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, color, 2)
        scale = 520 / p.shape[1]
        p = cv2.resize(p, (520, int(p.shape[0] * scale)))
        ok2, jp = cv2.imencode(".jpg", p, [cv2.IMWRITE_JPEG_QUALITY, 70])
        return base64.b64encode(jp.tobytes()).decode() if ok2 else ""

    if cls_idx < 0:
        # 네거티브: 박스 없는 빈 라벨 (배경 학습용)
        (OUT / "labels" / f"{stem}.txt").write_text("")
        return {"ok": True, "msg": f"{stem} 저장 (네거티브, 박스 없음)", "counts": counts(),
                "preview": preview(img)}

    # 위치는 모델이 찾고 (클래스 무관 최고 신뢰도 박스), 클래스는 사람이 지정
    r = model.predict(img, conf=0.15, imgsz=640, verbose=False)[0]
    if len(r.boxes) == 0:
        # 못 찾으면 이미지 중앙 60% 박스로 저장하고 사람이 나중에 확인하도록 표시
        (OUT / "labels" / f"{stem}.txt").write_text(f"{cls_idx} 0.5 0.5 0.6 0.6\n")
        cb = [w * 0.2, h * 0.2, w * 0.8, h * 0.8]
        return {"ok": True, "msg": f"{stem} 저장 ⚠ 박스 미검출 — 중앙 60% 가정(검수 필요)",
                "counts": counts(), "warn": True,
                "preview": preview(img, cb, (0, 140, 255), name + " (assumed)")}

    b = max(r.boxes, key=lambda bb: float(bb.conf))
    x1, y1, x2, y2 = b.xyxy[0].tolist()
    cx, cy = (x1 + x2) / 2 / w, (y1 + y2) / 2 / h
    bw, bh = (x2 - x1) / w, (y2 - y1) / h
    (OUT / "labels" / f"{stem}.txt").write_text(f"{cls_idx} {cx:.6f} {cy:.6f} {bw:.6f} {bh:.6f}\n")

    det_name = model.names[int(b.cls)]
    note = "" if det_name == name else f" (모델 의견: {det_name} — 사람 라벨 우선 적용)"
    return {"ok": True, "msg": f"{stem} 저장{note}",
            "box": [round(v) for v in (x1, y1, x2, y2)], "counts": counts(),
            "preview": preview(img, (x1, y1, x2, y2), (0, 190, 0),
                               "%s (box %.0f%%)" % (name, float(b.conf) * 100))}


PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>Fine-tune Collector</title>
<style>body{background:#111;color:#eee;font-family:sans-serif;margin:0;padding:10px;display:flex;gap:14px}
video{max-width:64%;border:2px solid #333;border-radius:4px}
#panel{flex:1}#log{color:#8f8;min-height:3em}table{border-collapse:collapse;width:100%}
td,th{border:1px solid #333;padding:4px 8px;font-size:14px}kbd{background:#333;padding:1px 7px;border-radius:3px}
.flash{animation:f .3s}@keyframes f{0%{border-color:#0f0}100%{border-color:#333}}</style></head><body>
<video id="v" autoplay playsinline></video>
<div id="panel">
<h3>수집·라벨링 (숫자키를 누르면 촬영+저장)</h3>
<table><tr><th>키</th><th>클래스</th><th>저장 수</th></tr>
<tr><td><kbd>1</kbd></td><td>투명페트</td><td id="c-pet_transparent">0</td></tr>
<tr><td><kbd>2</kbd></td><td>플라스틱</td><td id="c-plastic">0</td></tr>
<tr><td><kbd>3</kbd></td><td>캔</td><td id="c-can">0</td></tr>
<tr><td><kbd>4</kbd></td><td>유리</td><td id="c-glass">0</td></tr>
<tr><td><kbd>5</kbd></td><td>종이</td><td id="c-paper">0</td></tr>
<tr><td><kbd>6</kbd></td><td>종이팩</td><td id="c-paper_pack">0</td></tr>
<tr><td><kbd>7</kbd></td><td>비닐</td><td id="c-vinyl">0</td></tr>
<tr><td><kbd>8</kbd></td><td>스티로폼</td><td id="c-styrofoam">0</td></tr>
<tr><td><kbd>9</kbd></td><td>건전지</td><td id="c-battery">0</td></tr>
<tr><td><kbd>0</kbd></td><td>네거티브(비대상/빈판)</td><td id="c-negative">0</td></tr>
<tr><td><kbd>x</kbd></td><td style="color:#f88">마지막 저장 취소(삭제)</td><td>-</td></tr></table>
<p id="log">카메라 시작 중...</p>
<p style="margin:4px 0 2px;color:#aaa;font-size:13px">마지막 저장 샘플 (자동 박스 확인):</p>
<img id="prev" style="max-width:100%;border:1px solid #333;border-radius:4px">
<p style="color:#888;font-size:13px">요령: 물체당 8~12장 (회전·눕힘·찌그림 변화).
박스는 기존 모델이 자동으로 잡고, 클래스는 누른 키가 우선 적용됩니다.</p>
</div>
<script>
const v=document.getElementById('v'),log=document.getElementById('log');
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
startCam(m=>log.textContent=m+' — 물체를 놓고 숫자키를 누르세요');
let busy=false;
document.addEventListener('keydown',async e=>{
  if(e.key==='x'||e.key==='X'){
    if(busy)return;busy=true;
    try{const r=await fetch('/undo',{method:'POST'});const j=await r.json();
      log.textContent=j.msg;
      if(j.counts){document.querySelectorAll('[id^="c-"]').forEach(el=>el.textContent='0');
        for(const[k,n]of Object.entries(j.counts)){
          const el=document.getElementById('c-'+k);if(el)el.textContent=n;}}
    }catch(err){log.textContent='취소 실패: '+err;}
    busy=false;return;
  }
  if(busy||!/^[0-9]$/.test(e.key)||!v.videoWidth)return;
  busy=true;
  cv.width=v.videoWidth;cv.height=v.videoHeight;
  cv.getContext('2d').drawImage(v,0,0);
  cv.toBlob(async b=>{
    try{
      const r=await fetch('/save?key='+e.key,{method:'POST',body:b});
      const j=await r.json();
      log.textContent=j.msg;
      if(j.preview)document.getElementById('prev').src='data:image/jpeg;base64,'+j.preview;
      v.classList.add('flash');setTimeout(()=>v.classList.remove('flash'),300);
      if(j.counts)for(const[k,n]of Object.entries(j.counts)){
        const el=document.getElementById('c-'+k);if(el)el.textContent=n;}
    }catch(err){log.textContent='저장 실패: '+err;}
    busy=false;
  },'image/jpeg',0.92);
});
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
        q = parse_qs(urlparse(self.path).query)
        if urlparse(self.path).path == "/undo":
            if recent:
                stem = recent.pop()
                (OUT / "images" / f"{stem}.jpg").unlink(missing_ok=True)
                (OUT / "labels" / f"{stem}.txt").unlink(missing_ok=True)
                res = {"ok": True, "msg": f"삭제됨: {stem}", "counts": counts()}
            else:
                res = {"ok": False, "msg": "취소할 저장이 없습니다", "counts": counts()}
            body = json.dumps(res, ensure_ascii=False).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            print("[%s] %s" % (time.strftime("%H:%M:%S"), res["msg"]))
            return
        key = q.get("key", ["?"])[0]
        n = int(self.headers.get("Content-Length", 0))
        data = self.rfile.read(n)
        res = save_sample(data, key) if key in KEYMAP else {"ok": False, "msg": "잘못된 키"}
        body = json.dumps(res, ensure_ascii=False).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        print("[%s] %s" % (time.strftime("%H:%M:%S"), res.get("msg", "")))

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    print("수집 서버 시작 — 크롬에서 http://localhost:%d" % PORT)
    print("저장 위치: %s" % OUT)
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
