#!/usr/bin/env python3
"""카메라 사진 감시 → Run B-17 모델 판정 데모 (채택 후처리 적용판).

Windows 카메라 앱으로 찍거나(그림\\Camera Roll), 이미지를
바탕화면 '분류테스트' 폴더에 넣으면 즉시 판정.

판정 규칙 (docs/03_학습_보고서 채택안):
  1) 분배 = 품목 병합 (pet+pet_dirty→페트류 …) — 4품목만 수거함으로
  2) 오염 = dirty 신뢰도가 임계값(0.70) 이상일 때만 판별불가 + 세척 안내
  3) 종이·비닐 등 비대상 품목 = 판별불가 + 배출 코칭 안내
  4) 서로 다른 물체 2개 이상 = 복수 투입 → 리턴

사용법:  python3 watch_and_predict.py            # 감시 모드
        python3 watch_and_predict.py 이미지경로   # 단발 판정
"""
import sys
import time
from pathlib import Path

from ultralytics import YOLO

MODEL = Path(__file__).resolve().parent.parent / "models/runB17_yolo11n_17cls_best.pt"
WATCH_DIRS = [
    Path("/mnt/c/Users/wjswp/OneDrive/그림/Camera Roll"),
    Path("/mnt/c/Users/wjswp/OneDrive/Desktop/분류테스트"),
]
OUT_DIR = Path("/mnt/c/Users/wjswp/OneDrive/Desktop/분류테스트/결과")
EXTS = {".jpg", ".jpeg", ".png", ".bmp"}

CONF_TH = 0.50    # 검출 채택 임계값
DIRTY_TH = 0.70   # 오염 확정 임계값 (정밀도 우선 — 애매하면 클린 취급)

# 클래스 → (품목 그룹, 한글명, 오염여부)
GROUP = {
    "pet_transparent": ("pet", "투명페트", False), "pet_dirty": ("pet", "투명페트", True),
    "plastic": ("plastic", "플라스틱", False),     "plastic_dirty": ("plastic", "플라스틱", True),
    "can": ("can", "캔", False),                   "can_dirty": ("can", "캔", True),
    "glass": ("glass", "유리", False),             "glass_dirty": ("glass", "유리", True),
    "paper": ("paper", "종이", False),             "paper_dirty": ("paper", "종이", True),
    "paper_pack": ("paper_pack", "종이팩", False), "paper_pack_dirty": ("paper_pack", "종이팩", True),
    "vinyl": ("vinyl", "비닐", False),             "vinyl_dirty": ("vinyl", "비닐", True),
    "styrofoam": ("styrofoam", "스티로폼", False), "styrofoam_dirty": ("styrofoam", "스티로폼", True),
    "battery": ("battery", "건전지", False),
}
BINS = {"pet", "plastic", "can", "glass"}   # 실제 수거함이 있는 품목
COACH = {  # 비대상 품목 배출 코칭 문구
    "paper": "종이류 수거함에 배출해 주세요",
    "paper_pack": "종이팩 전용 수거함에 배출해 주세요 (일반 종이와 분리!)",
    "vinyl": "비닐류 수거함에 배출해 주세요",
    "styrofoam": "스티로폼 수거함에 배출해 주세요",
    "battery": "폐건전지 수거함에 배출해 주세요",
}


def _iou(a, b):
    x1, y1 = max(a[0], b[0]), max(a[1], b[1])
    x2, y2 = min(a[2], b[2]), min(a[3], b[3])
    inter = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    ar = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter
    return inter / (ar + 1e-9)


def merge_overlaps(raw, iou_th=0.55):
    """겹친 박스 = 같은 물체 → 최고 신뢰도 클래스 하나만 남김.
    (NMS가 클래스별로 도므로 한 물체에 서로 다른 클래스가 함께 남는 것을 방지)"""
    kept = []
    for name, conf, box in sorted(raw, key=lambda d: -d[1]):
        if not any(_iou(k[2], box) >= iou_th for k in kept):
            kept.append((name, conf, box))
    return [(n, c) for n, c, _ in kept]


def judge(model, img_path: Path):
    r = model.predict(str(img_path), conf=0.25, imgsz=640, verbose=False)[0]
    raw = [(model.names[int(b.cls)], float(b.conf), b.xyxy[0].tolist()) for b in r.boxes]
    dets = merge_overlaps(raw)
    strong = [d for d in dets if d[1] >= CONF_TH]

    if not dets:
        verdict = "판별불가 (물체 미검출 — 빈 스테이지?)"
    elif len(strong) >= 2 and len({GROUP[n][0] for n, _ in strong}) >= 2:
        items = ", ".join(f"{GROUP[n][1]}{'(오염)' if GROUP[n][2] else ''}" for n, _ in strong)
        verdict = f"복수 투입 감지 [{items}] → 리턴/판별불가"
    else:
        # 같은 품목의 클린/오염 중복 검출은 1개 물체로 간주 → 최고 신뢰도 기준
        name, conf = max(strong or dets, key=lambda d: d[1])
        group, kor, is_dirty = GROUP[name]
        if conf < CONF_TH:
            verdict = f"판별불가 (최고 신뢰도 {kor} {conf:.0%} < {CONF_TH:.0%})"
        elif group in BINS:
            if is_dirty and conf >= DIRTY_TH:
                verdict = f"오염된 {kor} ({conf:.0%}) → 판별불가함 · \"세척 후 다시 배출해 주세요\""
            else:
                tag = " (오염 의심이나 임계값 미만 → 클린 취급)" if is_dirty else ""
                verdict = f"{kor} ({conf:.0%}) → {kor}함으로 분배{tag}"
        else:
            state = "오염된 " if (is_dirty and conf >= DIRTY_TH) else ""
            verdict = f"{state}{kor} ({conf:.0%}) → 판별불가함 · \"{COACH[group]}\""

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out = OUT_DIR / f"pred_{img_path.stem}.jpg"
    r.save(str(out))
    print(f"[{time.strftime('%H:%M:%S')}] {img_path.name}")
    print(f"    검출: {[(GROUP[n][1] + ('·오염' if GROUP[n][2] else ''), f'{c:.0%}') for n, c in dets] or '없음'}")
    print(f"    ▶ 판정: {verdict}")
    print(f"    결과 이미지: 분류테스트\\결과\\{out.name}")


def main():
    model = YOLO(str(MODEL))
    if len(sys.argv) > 1:
        judge(model, Path(sys.argv[1]))
        return

    print("감시 시작 (Run B-17 · 17클래스) — 카메라로 찍거나 '분류테스트' 폴더에 넣으세요. Ctrl+C 종료")
    seen = {p for d in WATCH_DIRS if d.exists() for p in d.iterdir()}
    while True:
        time.sleep(1)
        for d in WATCH_DIRS:
            if not d.exists():
                continue
            for p in sorted(d.iterdir()):
                if p in seen or p.suffix.lower() not in EXTS:
                    continue
                seen.add(p)
                try:
                    time.sleep(0.5)
                    judge(model, p)
                except Exception as e:
                    print(f"  오류({p.name}): {e}")


if __name__ == "__main__":
    main()
