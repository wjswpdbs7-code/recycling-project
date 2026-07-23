#!/usr/bin/env python3
"""AI Hub 생활폐기물 라벨(JSON) → YOLO 형식 변환.

사용법:
  python3 convert_aihub_to_yolo.py <원천데이터_dir> <라벨링데이터_dir> <출력_dir>

- 클래스: 공식 가이드라인(2023-11-08 수정판) 매핑표 기준 4클래스.
    0 pet_transparent: c_5_01(투명페트+부속품), c_5_02(투명페트)
    1 plastic:         c_6
    2 can:             c_3
    3 glass:           c_4_01_*(재사용), c_4_02_*(갈/녹/백), c_4_03(기타) — 부속품 포함
  이물질(+_01 오염 계열)·종이(c_1,c_2)·비닐(c_7)·스티로폼(c_8)·건전지(c_9)는 비대상.
  비대상 박스가 하나라도 있는 이미지는 통째로 제외(미라벨 객체가 배경으로 학습되는 것 방지).
- 이미지 크기: 라벨 JSON의 Info.RESOLUTION("W/H") 사용, 없으면 PIL 시도.
- 출력: images/{train,val}, labels/{train,val}, data.yaml (9:1 분할)
"""
import json
import random
import shutil
import sys
from pathlib import Path


CLASSES_BY_MODE = {
    "4": ["pet_transparent", "plastic", "can", "glass"],
    "8": ["pet_transparent", "plastic", "can", "glass",
          "pet_dirty", "plastic_dirty", "can_dirty", "glass_dirty"],
    "9": ["pet_transparent", "plastic", "can", "glass",
          "pet_dirty", "plastic_dirty", "can_dirty", "glass_dirty", "other"],
    # 아파트 규정 풀스펙: 클린 9 + 품목별 오염 8. other 없음(전 품목 커버)
    "17": ["pet_transparent", "plastic", "can", "glass",
           "paper", "paper_pack", "vinyl", "styrofoam", "battery",
           "pet_dirty", "plastic_dirty", "can_dirty", "glass_dirty",
           "paper_dirty", "paper_pack_dirty", "vinyl_dirty", "styrofoam_dirty"],
}

CLEAN_17 = {"c_1": 4, "c_2_01": 5, "c_2_02": 5, "c_7": 6,
            "c_8_01": 7, "c_8_02": 7, "c_9": 8}
DIRTY_17 = {"c_5_01_01": 9, "c_5_02_01": 9, "c_6_01": 10, "c_3_01": 11, "c_4_03_01": 12,
            "c_1_01": 13, "c_2_02_01": 14, "c_7_01": 15, "c_8_01_01": 16}

# 오염(이물질) 계열 — 대상 품목의 오염만 dirty 클래스로, 나머지는 other/제외
DIRTY = {"c_1_01", "c_2_02_01", "c_3_01", "c_4_03_01",
         "c_5_01_01", "c_5_02_01", "c_6_01", "c_7_01", "c_8_01_01"}

DIRTY_MAP = {"c_5_01_01": 4, "c_5_02_01": 4, "c_6_01": 5, "c_3_01": 6, "c_4_03_01": 7}


def map_class(name: str, mode: str):
    """공식 코드 → 클래스 인덱스. 비대상이면 None(해당 이미지 제외 사유)."""
    if name in ("c_5_01", "c_5_02"):
        return 0
    if name == "c_6":
        return 1
    if name == "c_3":
        return 2
    if name.startswith(("c_4_01", "c_4_02", "c_4_03")) and name not in DIRTY:
        return 3
    if mode in ("8", "9") and name in DIRTY_MAP:
        return DIRTY_MAP[name]
    if mode == "9":
        return 8  # 종이·종이팩·비닐·스티로폼·건전지 및 그 오염 → other
    if mode == "17":
        if name in CLEAN_17:
            return CLEAN_17[name]
        if name in DIRTY_17:
            return DIRTY_17[name]
    return None


def image_size(info: dict, img_path: Path):
    res = (info or {}).get("RESOLUTION", "")
    if "/" in res:
        w, h = res.split("/")[:2]
        try:
            return int(w), int(h)
        except ValueError:
            pass
    try:
        from PIL import Image
        with Image.open(img_path) as im:
            return im.size
    except Exception:
        return None


def main(src_dir: Path, label_dir: Path, out_dir: Path, mode: str = "4"):
    CLASSES = CLASSES_BY_MODE[mode]
    jsons = sorted(label_dir.rglob("*.json"))
    images = {p.name: p for p in src_dir.rglob("*.jpg")}
    print(f"라벨 {len(jsons)}개, 이미지 {len(images)}개")

    cls_idx = {c: i for i, c in enumerate(CLASSES)}

    pairs = []
    stats = {"no_image": 0, "no_size": 0, "has_nontarget": 0, "dirty_only": 0, "empty": 0}
    box_counts = [0] * len(CLASSES)
    for j in jsons:
        # JSON 내부 Image 필드는 파일명과 불일치 사례가 있어 파일명 기준 매칭
        img = images.get(j.stem + ".jpg")
        if img is None:
            stats["no_image"] += 1
            continue
        data = json.loads(j.read_text(encoding="utf-8"))
        size = image_size(data.get("Info"), img)
        if not size:
            stats["no_size"] += 1
            continue
        W, H = size
        lines = []
        nontarget = dirty = False
        for o in data.get("objects", []):
            if o.get("annotation_type") != "box":
                continue
            cls = map_class(o["class_name"], mode)
            if cls is None:
                nontarget = True
                if o["class_name"] in DIRTY:
                    dirty = True
                continue
            c = o["annotation"]["coord"]
            x, y, w, h = c["x"], c["y"], c["width"], c["height"]
            cx, cy = (x + w / 2) / W, (y + h / 2) / H
            nw, nh = w / W, h / H
            if not (0 < nw <= 1 and 0 < nh <= 1):
                continue
            cx, cy = min(max(cx, 0), 1), min(max(cy, 0), 1)
            lines.append(f"{cls} {cx:.6f} {cy:.6f} {nw:.6f} {nh:.6f}")
            box_counts[cls] += 1
        if nontarget:
            stats["dirty_only" if (dirty and not lines) else "has_nontarget"] += 1
            for i, l in enumerate(lines):
                box_counts[int(l.split()[0])] -= 1
            continue
        if not lines:
            stats["empty"] += 1
            continue
        pairs.append((img, lines))

    print(f"변환 대상 {len(pairs)}쌍 / 제외 사유: {stats}")
    print("클래스별 박스 수:", dict(zip(CLASSES, box_counts)))
    random.seed(42)
    random.shuffle(pairs)
    n_val = max(1, len(pairs) // 10)
    splits = {"val": pairs[:n_val], "train": pairs[n_val:]}

    for split, items in splits.items():
        (out_dir / f"images/{split}").mkdir(parents=True, exist_ok=True)
        (out_dir / f"labels/{split}").mkdir(parents=True, exist_ok=True)
        for img, lines in items:
            shutil.copy2(img, out_dir / f"images/{split}/{img.name}")
            (out_dir / f"labels/{split}/{img.stem}.txt").write_text("\n".join(lines) + "\n")
        print(f"{split}: {len(items)}장")

    yaml = [f"path: {out_dir.resolve()}", "train: images/train", "val: images/val", "", "names:"]
    yaml += [f"  {i}: {c}" for c, i in sorted(cls_idx.items(), key=lambda kv: kv[1])]
    (out_dir / "data.yaml").write_text("\n".join(yaml) + "\n")
    print(f"완료 → {out_dir}/data.yaml")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    main(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]),
         sys.argv[4] if len(sys.argv) == 5 else "4")
