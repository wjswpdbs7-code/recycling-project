#!/usr/bin/env python3
"""라벨 폴더 전체를 스캔해 모드 4/9 기준 활용 장수·클래스 분포 리포트."""
import json
import pathlib
import sys
from collections import Counter

sys.path.insert(0, ".")
from convert_aihub_to_yolo import CLASSES_BY_MODE, map_class

label_dir = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "indoor/label")
jsons = sorted(label_dir.rglob("*.json"))
print(f"총 라벨 {len(jsons)}개")

for mode in ("4", "9", "17"):
    names = CLASSES_BY_MODE[mode]
    usable = 0
    boxes = Counter()
    per_folder = Counter()
    for j in jsons:
        objs = [o for o in json.loads(j.read_text(encoding="utf-8")).get("objects", [])
                if o.get("annotation_type") == "box"]
        cls = [map_class(o["class_name"], mode) for o in objs]
        if cls and all(c is not None for c in cls):
            usable += 1
            per_folder[j.stem.split("_")[0]] += 1
            for c in cls:
                boxes[names[c]] += 1
    print(f"\n=== 모드 {mode}: 활용 {usable}/{len(jsons)}장 ({usable*100//max(1,len(jsons))}%) ===")
    for k, v in boxes.most_common():
        print(f"  {k:>16}: {v:,}")
    print("  폴더별:", dict(sorted(per_folder.items())))
