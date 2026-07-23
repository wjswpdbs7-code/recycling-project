#!/usr/bin/env python3
"""Jetson Nano TensorRT 추론 — C920 촬영 → Run B-17 판정 (채택 후처리 적용).

사용법:
  python3 jetson_infer.py            # 카메라 1장 촬영 후 판정
  python3 jetson_infer.py 이미지.jpg  # 파일 판정
  python3 jetson_infer.py --loop      # 연속 판정 (Ctrl+C 종료)

판정 규칙은 노트북판(watch_and_predict.py)과 동일:
  분배 = 품목 병합 / 오염 = DIRTY_TH 이상 / 비대상 = 배출 코칭 / 다품목 2개 이상 = 복수 투입
"""
import sys
import time

import cv2
import numpy as np
import tensorrt as trt
import pycuda.driver as cuda
import pycuda.autoinit  # noqa: F401  (CUDA 컨텍스트 초기화)

ENGINE = "/home/jetson/runB17_ft1.engine"  # ft1 시연 특화 (롤백: runB17.engine)
IMGSZ = 640
CONF_TH = 0.40
DIRTY_TH = 0.70
NMS_IOU = 0.45

# ─── 운용 설정 (일정 축소판) ───────────────────────────────────
# IGNORE_DIRTY=True 면 오염 클래스를 같은 품목의 클린으로 간주한다.
#   예) plastic_dirty(0.85) → 플라스틱으로 분배. 오염 판정·세척 안내 없음.
#   재학습 불필요 — 모델은 17종 그대로, 판정만 병합.
IGNORE_DIRTY = True

# 실제 수거함 품목 (회전판 0/60/120도 순서와 일치). 나머지는 전부 '기타'(180도).
# 투명페트는 재질 기준으로 플라스틱함에 병합 (GROUP에서 pet→plastic 매핑).
BIN_ITEMS = ["paper", "plastic", "vinyl"]
# ────────────────────────────────────────────────────────────

NAMES = ["pet_transparent", "plastic", "can", "glass",
         "paper", "paper_pack", "vinyl", "styrofoam", "battery",
         "pet_dirty", "plastic_dirty", "can_dirty", "glass_dirty",
         "paper_dirty", "paper_pack_dirty", "vinyl_dirty", "styrofoam_dirty"]

GROUP = {
    "pet_transparent": ("plastic", "투명페트", False), "pet_dirty": ("plastic", "투명페트", True),
    "plastic": ("plastic", "플라스틱", False),     "plastic_dirty": ("plastic", "플라스틱", True),
    "can": ("can", "캔", False),                   "can_dirty": ("can", "캔", True),
    "glass": ("glass", "유리", False),             "glass_dirty": ("glass", "유리", True),
    "paper": ("paper", "종이", False),             "paper_dirty": ("paper", "종이", True),
    "paper_pack": ("paper", "종이팩", False), "paper_pack_dirty": ("paper", "종이팩", True),
    "vinyl": ("vinyl", "비닐", False),             "vinyl_dirty": ("vinyl", "비닐", True),
    "styrofoam": ("styrofoam", "스티로폼", False), "styrofoam_dirty": ("styrofoam", "스티로폼", True),
    "battery": ("battery", "건전지", False),
}
BINS = set(BIN_ITEMS)          # 분배 대상 품목 (그 외는 '기타'함)
KOR = {"pet": "투명페트", "plastic": "플라스틱", "can": "캔", "glass": "유리",
       "paper": "종이", "paper_pack": "종이팩", "vinyl": "비닐",
       "styrofoam": "스티로폼", "battery": "건전지"}

# Pico로 보낼 명령 — 분배 품목은 SORT, 나머지는 전부 기타함(ETC)
CMD = {g: "SORT " + g.upper() for g in BIN_ITEMS}


class Engine:
    """TensorRT 엔진 래퍼 — 로드 1회, 추론 반복."""

    def __init__(self, path):
        logger = trt.Logger(trt.Logger.WARNING)
        with open(path, "rb") as f, trt.Runtime(logger) as rt:
            self.engine = rt.deserialize_cuda_engine(f.read())
        self.ctx = self.engine.create_execution_context()
        self.stream = cuda.Stream()
        self.bindings = []
        self.inputs, self.outputs = [], []
        for i in range(self.engine.num_bindings):
            shape = self.engine.get_binding_shape(i)
            size = int(np.prod(shape))
            dtype = trt.nptype(self.engine.get_binding_dtype(i))
            host = cuda.pagelocked_empty(size, dtype)
            dev = cuda.mem_alloc(host.nbytes)
            self.bindings.append(int(dev))
            entry = {"host": host, "dev": dev, "shape": tuple(shape)}
            (self.inputs if self.engine.binding_is_input(i) else self.outputs).append(entry)

    def infer(self, x):
        np.copyto(self.inputs[0]["host"], x.ravel())
        cuda.memcpy_htod_async(self.inputs[0]["dev"], self.inputs[0]["host"], self.stream)
        self.ctx.execute_async_v2(bindings=self.bindings, stream_handle=self.stream.handle)
        for o in self.outputs:
            cuda.memcpy_dtoh_async(o["host"], o["dev"], self.stream)
        self.stream.synchronize()
        o = self.outputs[0]
        return o["host"].reshape(o["shape"])


def letterbox(img, size=IMGSZ):
    """비율 유지 리사이즈 + 회색 패딩 (YOLO 표준 전처리)."""
    h, w = img.shape[:2]
    r = min(size / h, size / w)
    nh, nw = int(round(h * r)), int(round(w * r))
    resized = cv2.resize(img, (nw, nh), interpolation=cv2.INTER_LINEAR)
    canvas = np.full((size, size, 3), 114, dtype=np.uint8)
    top, left = (size - nh) // 2, (size - nw) // 2
    canvas[top:top + nh, left:left + nw] = resized
    return canvas, r, left, top


def preprocess(img):
    canvas, r, dx, dy = letterbox(img)
    x = canvas[:, :, ::-1].transpose(2, 0, 1)          # BGR→RGB, HWC→CHW
    x = np.ascontiguousarray(x, dtype=np.float32) / 255.0
    return x[None], r, dx, dy


def nms(boxes, scores, iou_th=NMS_IOU):
    """단순 NMS (클래스 무관 — 이미 클래스별로 나눠 호출)."""
    idx = scores.argsort()[::-1]
    keep = []
    while idx.size:
        i = idx[0]
        keep.append(i)
        if idx.size == 1:
            break
        xx1 = np.maximum(boxes[i, 0], boxes[idx[1:], 0])
        yy1 = np.maximum(boxes[i, 1], boxes[idx[1:], 1])
        xx2 = np.minimum(boxes[i, 2], boxes[idx[1:], 2])
        yy2 = np.minimum(boxes[i, 3], boxes[idx[1:], 3])
        inter = np.maximum(0, xx2 - xx1) * np.maximum(0, yy2 - yy1)
        a1 = (boxes[i, 2] - boxes[i, 0]) * (boxes[i, 3] - boxes[i, 1])
        a2 = (boxes[idx[1:], 2] - boxes[idx[1:], 0]) * (boxes[idx[1:], 3] - boxes[idx[1:], 1])
        iou = inter / (a1 + a2 - inter + 1e-9)
        idx = idx[1:][iou <= iou_th]
    return keep


def postprocess(out, r, dx, dy, shape, conf_th=0.25):
    """YOLO11 출력 (1, 4+nc, 8400) → [(이름, 신뢰도, xyxy), ...]"""
    pred = out[0]                      # (4+nc, 8400)
    if pred.shape[0] < pred.shape[1]:  # (4+nc, N) 형태 확인
        pred = pred.T                  # → (N, 4+nc)
    boxes_xywh = pred[:, :4]
    cls_scores = pred[:, 4:]
    cls_ids = cls_scores.argmax(1)
    confs = cls_scores.max(1)
    m = confs >= conf_th
    if not m.any():
        return []
    boxes_xywh, cls_ids, confs = boxes_xywh[m], cls_ids[m], confs[m]

    # cx,cy,w,h → x1,y1,x2,y2 (letterbox 역보정)
    xy = boxes_xywh[:, :2]
    wh = boxes_xywh[:, 2:4]
    x1y1 = (xy - wh / 2 - np.array([dx, dy])) / r
    x2y2 = (xy + wh / 2 - np.array([dx, dy])) / r
    boxes = np.concatenate([x1y1, x2y2], 1)
    boxes[:, 0::2] = boxes[:, 0::2].clip(0, shape[1])
    boxes[:, 1::2] = boxes[:, 1::2].clip(0, shape[0])

    dets = []
    for c in np.unique(cls_ids):
        s = cls_ids == c
        for i in nms(boxes[s], confs[s]):
            dets.append((NAMES[int(c)], float(confs[s][i]), boxes[s][i]))
    return sorted(dets, key=lambda d: -d[1])


def iou(a, b):
    x1 = max(a[0], b[0]); y1 = max(a[1], b[1])
    x2 = min(a[2], b[2]); y2 = min(a[3], b[3])
    inter = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    ar = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter
    return inter / (ar + 1e-9)


def cluster_objects(dets, iou_th=0.55):
    """겹치는 박스들을 '같은 물리적 물체' 하나로 묶는다.

    NMS는 클래스별로 수행되므로, 한 물체에 서로 다른 클래스 박스가
    동시에 남을 수 있다(예: 같은 페트병이 pet_transparent + plastic).
    이를 그대로 두면 '복수 투입'으로 오판하므로, IoU로 군집화해
    군집당 최고 신뢰도 클래스 하나만 대표로 남긴다.
    반환: [(대표이름, 대표신뢰도, 박스, [군집 내 (이름, 신뢰도)...]), ...]
    """
    clusters = []
    for name, conf, box in sorted(dets, key=lambda d: -d[1]):
        for c in clusters:
            if iou(c[2], box) >= iou_th:
                c[3].append((name, conf))
                break
        else:
            clusters.append([name, conf, box, [(name, conf)]])
    return clusters


def judge(dets):
    """채택 후처리 → (판정문구, Pico 명령 or None)"""
    # 겹친 박스를 하나의 물체로 병합 (한 물체 다중 판정 방지)
    dets = [(c[0], c[1], c[2]) for c in cluster_objects(dets)]
    strong = [d for d in dets if d[1] >= CONF_TH]
    if not dets:
        return "판별불가 (물체 미검출 — 빈 스테이지?)", None
    if len(strong) >= 2 and len({GROUP[n][0] for n, _, _ in strong}) >= 2:
        items = ", ".join("%s%s" % (GROUP[n][1], "(오염)" if GROUP[n][2] else "") for n, _, _ in strong)
        return "복수 투입 감지 [%s] → 리턴" % items, "REJECT MULTI"

    name, conf, _ = (strong or dets)[0]
    group, kor, is_dirty = GROUP[name]
    if conf < CONF_TH:
        return "판별불가 (최고 신뢰도 %s %.0f%% < %.0f%%)" % (kor, conf * 100, CONF_TH * 100), "REJECT LOWCONF"

    if group in BINS:
        # IGNORE_DIRTY=True면 오염 여부를 따지지 않고 품목으로만 분배
        if not IGNORE_DIRTY and is_dirty and conf >= DIRTY_TH:
            return "오염된 %s (%.0f%%) → 판별불가함 · 세척 후 배출" % (kor, conf * 100), "REJECT DIRTY"
        tag = " (오염 감지되었으나 무시 설정)" if (is_dirty and IGNORE_DIRTY) else ""
        return "%s (%.0f%%) → %s함으로 분배%s" % (kor, conf * 100, kor, tag), CMD[group]

    # 분배 대상 외 품목 = 수거함 없음 → 리턴 (3칸 체계)
    return "%s (%.0f%%) → 분류 대상 아님 · 리턴" % (kor, conf * 100), "REJECT OTHER"


def capture(cam_id=0, warmup=5):
    cap = cv2.VideoCapture(cam_id)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
    frame = None
    for _ in range(warmup):        # 자동노출 안정화
        ok, f = cap.read()
        if ok:
            frame = f
    cap.release()
    return frame


def run_once(eng, img, save=None):
    t0 = time.time()
    x, r, dx, dy = preprocess(img)
    t1 = time.time()
    out = eng.infer(x)
    t2 = time.time()
    dets = postprocess(out, r, dx, dy, img.shape[:2])
    verdict, cmd = judge(dets)
    t3 = time.time()

    print("검출: %s" % ([(GROUP[n][1] + ("·오염" if GROUP[n][2] else ""), "%.0f%%" % (c * 100))
                        for n, c, _ in dets] or "없음"))
    print("▶ 판정: %s" % verdict)
    print("  Pico 명령: %s" % (cmd or "-"))
    print("  시간: 전처리 %.0fms / 추론 %.0fms / 후처리 %.0fms" %
          ((t1 - t0) * 1000, (t2 - t1) * 1000, (t3 - t2) * 1000))

    if save:
        for n, c, b in dets:
            x1, y1, x2, y2 = map(int, b)
            cv2.rectangle(img, (x1, y1), (x2, y2), (0, 200, 255), 2)
            cv2.putText(img, "%s %.2f" % (n, c), (x1, max(15, y1 - 6)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 200, 255), 2)
        cv2.imwrite(save, img)
        print("  결과 이미지: %s" % save)
    return verdict, cmd


def main():
    print("TensorRT 엔진 로드 중...")
    eng = Engine(ENGINE)
    print("준비 완료\n")

    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    loop = "--loop" in sys.argv

    if args:
        img = cv2.imread(args[0])
        if img is None:
            print("이미지를 읽을 수 없습니다: %s" % args[0])
            return
        run_once(eng, img, save="/home/jetson/result.jpg")
        return

    while True:
        img = capture()
        if img is None:
            print("카메라 촬영 실패 (/dev/video0 확인)")
            return
        print("[%s] 촬영 %dx%d" % (time.strftime("%H:%M:%S"), img.shape[1], img.shape[0]))
        run_once(eng, img, save="/home/jetson/result.jpg")
        if not loop:
            return
        print("-" * 50)
        time.sleep(2)


if __name__ == "__main__":
    main()
