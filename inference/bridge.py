#!/usr/bin/env python3
"""전자동 사이클 브리지 — 브라우저(웹캠+시리얼) ↔ WSL 모델 서버.

동작:
  Pico "DETECT" 수신 → 1초간 10프레임 촬영 → 각 프레임 판정 →
  ★ 10장 다수결(가장 많이 나온 분류)로 최종 판별 ★ →
  "SORT xxx" / "REJECT xxx"를 Pico로 송신 → 기구 동작

실행:  python3 bridge.py
접속:  크롬에서 http://localhost:8092
       ① [시리얼 연결] 클릭 → COM5 선택 (Web Serial API)
       ② 카메라 허용 (C920 선택)
       ③ 이후 전자동 — 물체 올리면 사이클이 알아서 돈다

판정 서버는 프레임당 JSON(명령·판정문)을 돌려주고, 다수결은 브라우저가 수행.
"""
import base64
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import cv2
import numpy as np
from ultralytics import YOLO

MODEL = Path(__file__).resolve().parent.parent / "models/runB17_yolo11n_17cls_best.pt"
PORT = 8092
CONF_TH = 0.40

# ── 3칸 체계 (0=종이 / 90=플라스틱 / 180=비닐, 그 외 리턴) ──
GROUP = {
    "pet_transparent": "plastic", "pet_dirty": "plastic",
    "plastic": "plastic", "plastic_dirty": "plastic",
    "paper": "paper", "paper_dirty": "paper",
    "paper_pack": "paper", "paper_pack_dirty": "paper",   # 종이팩→종이
    "vinyl": "vinyl", "vinyl_dirty": "vinyl",
    "can": "can", "can_dirty": "can",
    "glass": "glass", "glass_dirty": "glass",
    "styrofoam": "styrofoam", "styrofoam_dirty": "styrofoam",
    "battery": "battery",
}
BINS = {"paper", "plastic", "vinyl"}

model = YOLO(str(MODEL))


def iou(a, b):
    x1, y1 = max(a[0], b[0]), max(a[1], b[1])
    x2, y2 = min(a[2], b[2]), min(a[3], b[3])
    inter = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    ar = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter
    return inter / (ar + 1e-9)


def merge_overlaps(raw, iou_th=0.55):
    kept = []
    for name, conf, box in sorted(raw, key=lambda d: -d[1]):
        if not any(iou(k[2], box) >= iou_th for k in kept):
            kept.append((name, conf, box))
    return kept


def judge_frame(img):
    """1프레임 판정 → (명령문자열, 설명, dets)"""
    r = model.predict(img, conf=0.25, imgsz=640, verbose=False)[0]
    raw = [(model.names[int(b.cls)], float(b.conf), b.xyxy[0].tolist()) for b in r.boxes]
    dets = merge_overlaps(raw)
    strong = [d for d in dets if d[1] >= CONF_TH]

    if not dets:
        return "NONE", "미검출", dets
    if len(strong) >= 2 and len({GROUP[n] for n, _, _ in strong}) >= 2:
        return "REJECT MULTI", "복수 투입", dets
    name, conf, _ = (strong or dets)[0]
    g = GROUP[name]
    if conf < CONF_TH:
        return "REJECT LOWCONF", "저신뢰 %s %.0f%%" % (name, conf * 100), dets
    if g in BINS:
        return "SORT " + g.upper(), "%s %.0f%%" % (name, conf * 100), dets
    return "REJECT OTHER", "비대상 %s %.0f%%" % (name, conf * 100), dets


def annotate(img, dets, header):
    for n, c, b in dets:
        x1, y1, x2, y2 = map(int, b)
        cv2.rectangle(img, (x1, y1), (x2, y2), (0, 190, 0), 2)
        cv2.putText(img, "%s %.2f" % (n, c), (x1, max(16, y1 - 6)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 190, 0), 2)
    cv2.rectangle(img, (0, 0), (img.shape[1], 40), (0, 0, 0), -1)
    cv2.putText(img, header, (10, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.75, (0, 220, 255), 2)
    scale = 520 / img.shape[1]
    img = cv2.resize(img, (520, int(img.shape[0] * scale)))
    ok, jp = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 72])
    return base64.b64encode(jp.tobytes()).decode() if ok else ""


PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>Auto Sorter Bridge</title>
<style>body{background:#111;color:#eee;font-family:sans-serif;margin:0;padding:10px;display:flex;gap:14px}
video{max-width:56%;border:2px solid #333;border-radius:4px}
#panel{flex:1}button{font-size:16px;padding:8px 18px;margin:2px;border-radius:5px;border:0;cursor:pointer}
#log{background:#000;border:1px solid #333;height:200px;overflow-y:auto;font-size:13px;
font-family:monospace;padding:6px;white-space:pre-wrap}
#state{font-size:20px;font-weight:bold;color:#8f8}
img{max-width:100%;border:1px solid #333;border-radius:4px}</style></head><body>
<video id="v" autoplay playsinline></video>
<div id="panel">
<h3>전자동 분류 사이클 (10프레임 다수결)</h3>
<button id="btn" style="background:#2b6">1. 시리얼 연결 (COM5)</button>
<span id="state">시리얼 미연결</span>
<div id="log"></div>
<p id="cap" style="margin:6px 0 2px;color:#0cf;font-size:14px">실시간 미리보기 준비 중...</p>
<img id="prev">
</div>
<script>
const v=document.getElementById('v'),logEl=document.getElementById('log'),
      st=document.getElementById('state'),prev=document.getElementById('prev'),
      cap=document.getElementById('cap');
function log(s){logEl.textContent+=s+"\\n";logEl.scrollTop=logEl.scrollHeight;}
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
startCam(m=>{log(m);liveLoop();});

// 대기 중 실시간 미리보기 (busy 동안은 cycle이 프레임별로 갱신)
let lastLive=null;
async function liveLoop(){
  while(true){
    if(!busy&&v.videoWidth){
      try{
        const b=await grab();
        const r=await fetch('/judge',{method:'POST',body:b});
        const j=await r.json();
        prev.src='data:image/jpeg;base64,'+j.preview;
        cap.textContent='실시간: '+j.cmd+' ('+j.desc+')';
        if(j.cmd!==lastLive){log('[감지] '+j.cmd+' ('+j.desc+')');lastLive=j.cmd;}
      }catch(e){}
    }
    await new Promise(r=>setTimeout(r,400));
  }
}

let port=null,writer=null,busy=false;
document.getElementById('btn').onclick=async()=>{
  try{
    port=await navigator.serial.requestPort();
    await port.open({baudRate:115200});
    writer=port.writable.getWriter();
    st.textContent='자동 사이클 대기 중';
    log('시리얼 연결됨 — 물체를 올리면 자동 진행');
    readLoop();
  }catch(e){log('시리얼 오류: '+e);}
};

async function readLoop(){
  const dec=new TextDecoderStream();
  port.readable.pipeTo(dec.writable);
  const reader=dec.readable.getReader();
  let buf='';
  while(true){
    const {value,done}=await reader.read();
    if(done)break;
    buf+=value;
    let i;
    while((i=buf.indexOf('\\n'))>=0){
      const line=buf.slice(0,i).trim();buf=buf.slice(i+1);
      if(!line)continue;
      log('[Pico] '+line);
      if(line.startsWith('DETECT')&&!busy)cycle();
    }
  }
}

function grab(){
  return new Promise(res=>{
    cv.width=v.videoWidth;cv.height=v.videoHeight;
    cv.getContext('2d').drawImage(v,0,0);
    cv.toBlob(b=>res(b),'image/jpeg',0.9);
  });
}

async function cycle(){
  busy=true;st.textContent='촬영 중 (10프레임/1초)...';
  // 1) 1초 동안 10프레임 버퍼링
  const frames=[];
  for(let i=0;i<10;i++){
    frames.push(await grab());
    await new Promise(r=>setTimeout(r,100));
  }
  // 2) 프레임별 판정
  st.textContent='분석 중...';
  const votes={};let previews={};
  for(let i=0;i<frames.length;i++){
    try{
      const r=await fetch('/judge',{method:'POST',body:frames[i]});
      const j=await r.json();
      votes[j.cmd]=(votes[j.cmd]||0)+1;
      previews[j.cmd]=j.preview;
      prev.src='data:image/jpeg;base64,'+j.preview;          // 프레임별 즉시 표시
      cap.textContent='분석 '+(i+1)+'/10: '+j.cmd+' ('+j.desc+')';
      log('  프레임'+(i+1)+': '+j.cmd+' ('+j.desc+')');
    }catch(e){log('  프레임'+(i+1)+' 오류: '+e);}
  }
  // 3) 다수결 (NONE 제외 우선, 전부 NONE이면 REJECT LOWCONF)
  let best=null,bn=-1;
  for(const[c,n]of Object.entries(votes)){
    if(c==='NONE')continue;
    if(n>bn){bn=n;best=c;}
  }
  if(!best)best='REJECT LOWCONF';
  log('★ 다수결: '+best+' ('+(bn>0?bn:0)+'/10표)');
  if(previews[best])prev.src='data:image/jpeg;base64,'+previews[best];
  cap.textContent='최종(다수결): '+best+' ('+(bn>0?bn:0)+'/10표)';
  // 4) Pico로 송신
  await writer.write(new TextEncoder().encode(best+'\\r\\n'));
  st.textContent='명령 전송: '+best+' — 기구 동작 중';
  setTimeout(()=>{st.textContent='자동 사이클 대기 중';busy=false;},4000);
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
        img = cv2.imdecode(np.frombuffer(self.rfile.read(n), np.uint8), cv2.IMREAD_COLOR)
        if img is None:
            res = {"cmd": "NONE", "desc": "디코드 실패", "preview": ""}
        else:
            cmd, desc, dets = judge_frame(img)
            res = {"cmd": cmd, "desc": desc, "preview": annotate(img, dets, cmd + "  " + desc)}
        body = json.dumps(res, ensure_ascii=False).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        print("[%s] %s %s" % (time.strftime("%H:%M:%S"), res["cmd"], res["desc"]))

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    print("브리지 서버 — 크롬에서 http://localhost:%d (시리얼 연결 버튼 → COM5)" % PORT)
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
