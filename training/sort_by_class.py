#!/usr/bin/env python3
"""AI Hub 원천/라벨 폴더를 받아 클래스별 열람용 폴더로 복사 정리.

사용법: python3 sort_by_class.py <원천_dir> <라벨_dir> <출력_dir>

- 박스가 1개인 이미지 → <출력>/<코드_한글명>/ 에 복사 (클래스별로 한 장씩 넘겨보기용)
- 박스가 여러 개인 이미지 → <출력>/_복수객체/ 에 복사
"""
import json
import shutil
import sys
from pathlib import Path

NAME = {
    "c_1": "종이", "c_1_01": "종이+이물질",
    "c_2_01": "종이팩", "c_2_02": "종이컵", "c_2_02_01": "종이컵+이물질",
    "c_3": "캔", "c_3_01": "캔+이물질",
    "c_4_01_01": "재사용유리+부속품", "c_4_01_02": "재사용유리",
    "c_4_02_01_01": "갈색유리+부속품", "c_4_02_01_02": "갈색유리",
    "c_4_02_02_01": "녹색유리+부속품", "c_4_02_02_02": "녹색유리",
    "c_4_02_03_01": "백색유리+부속품", "c_4_02_03_02": "백색유리",
    "c_4_03": "기타유리", "c_4_03_01": "기타유리+이물질",
    "c_5_01": "투명페트+부속품", "c_5_01_01": "투명페트+부속품+이물질",
    "c_5_02": "투명페트", "c_5_02_01": "투명페트+이물질",
    "c_6": "플라스틱", "c_6_01": "플라스틱+이물질",
    "c_7": "비닐", "c_7_01": "비닐+이물질",
    "c_8_01": "흰색스티로폼", "c_8_02": "컬러스티로폼", "c_8_01_01": "스티로폼+이물질",
    "c_9": "건전지",
}


def main(src_dir: Path, label_dir: Path, out_dir: Path):
    images = {p.stem: p for p in src_dir.rglob("*.jpg")}
    counts = {}
    for j in sorted(label_dir.rglob("*.json")):
        img = images.get(j.stem)
        if img is None:
            continue
        objs = [o for o in json.loads(j.read_text(encoding="utf-8")).get("objects", [])
                if o.get("annotation_type") == "box"]
        if not objs:
            continue
        if len(objs) == 1:
            code = objs[0]["class_name"]
            folder = f"{code}_{NAME.get(code, '미정의')}"
        else:
            folder = "_복수객체"
        dest = out_dir / folder
        dest.mkdir(parents=True, exist_ok=True)
        shutil.copy2(img, dest / img.name)
        counts[folder] = counts.get(folder, 0) + 1

    for k in sorted(counts):
        print(f"{counts[k]:>5}장  {k}")
    print(f"총 {sum(counts.values())}장 → {out_dir}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]))
