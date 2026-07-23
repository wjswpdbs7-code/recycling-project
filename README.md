# ♻️ 가정용 분리수거 자동분류기 (Recycling Auto-Sorter)

> 쓰레기를 올리면 **AI가 판별하고 기계가 알아서 분류**하는 온디바이스 엣지 AI 시스템.
> HRD 직업훈련 최종 프로젝트 (12주) — YOLO11 · TensorRT · Jetson Nano · Raspberry Pi Pico 2 W

```
[투입] → 초음파 감지 → 3초 안정화 → C920 10프레임 촬영(1초)
      → TensorRT 판정(GPU 50ms/장) → ★10프레임 다수결★
      → 회전판 정렬(종이 0°/플라스틱 90°/비닐 180°) → 투입구 개방 → 낙하
      → 판별불가·비대상은 문을 열지 않고 리턴 (오분배 원천 차단)
```

## 핵심 수치

| 지표 | 값 |
|---|---|
| 학습 데이터 | AI Hub 생활폐기물 102,271장 / 17클래스 / 활용률 100% |
| 모델 | YOLO11n (2.6M 파라미터, 5.3MB) |
| 공식 Validation | mAP50 **0.912** (품목 병합 시 91~98%) |
| 학습 비용 | 약 **$10** (RunPod A40) |
| 엣지 추론 | **49.6ms/프레임** (Jetson Nano, TensorRT FP16) |
| 실물 테스트 | 오분배 **0건** (애매하면 리턴하는 안전 설계) |
| 자동 시작 | 전원 인가 → **약 40초 후 무인 가동** (systemd) |

## 시스템 구성

- **Jetson Nano** — 두뇌: C920 촬영, TensorRT 추론, 다수결 판정, 웹 모니터링(MJPEG)
- **Pico 2 W** — 손발: 초음파 감지·서보 2개(투입구/회전판)·LCD 상태표시, 상태머신 펌웨어(C)
- **노트북** — 개발·모니터링 전용 (분리해도 기계는 자립 동작)

## 저장소 구조

| 경로 | 내용 |
|---|---|
| `docs/` | **전체 문서** — 진행일지·설계서·클래스 설계·학습 보고서·배선 가이드·제작도·[포트폴리오 기록](docs/05_포트폴리오_기록.md) |
| `docs/img/` | 학습 곡선, confusion matrix, 실물 테스트 캡처, CAD 렌더, 성능 차트 |
| `models/` | 학습 완료 가중치 (.pt) + ONNX 변환본 |
| `training/` | AI Hub JSON→YOLO 변환기, 학습 결과 로그·곡선 (원본 데이터셋 20GB는 제외) |
| `inference/` | 판정·데모 코드 — Jetson 브리지, 노트북 브리지, 수집 도구, 스트리밍 |
| `pico_sonar_test/` | **최종 구동부 펌웨어** (상태머신: 감지→분배→재무장, LCD 자가복구) |
| `pico_*` | 개발 과정의 테스트·진단 펌웨어들 (초음파/LCD/서보 진단 도구 포함) |
| `cad/` | OpenSCAD 파라메트릭 3D 설계 (검증식 내장) |

## 문서 읽는 순서 (발표자료·기술서 제작용)

1. [`docs/05_포트폴리오_기록.md`](docs/05_포트폴리오_기록.md) — **의사결정 서사·시행착오·수치 총정리** (발표의 뼈대)
2. [`docs/00_진행일지.md`](docs/00_진행일지.md) — 일자별 타임라인
3. [`docs/03_학습_보고서.md`](docs/03_학습_보고서.md) — 학습 상세 + 실물 테스트 분석
4. [`docs/02_클래스_설계.md`](docs/02_클래스_설계.md) / [`docs/06_프로토타입_제작도.md`](docs/06_프로토타입_제작도.md) — 설계 근거

## 재현 방법 (요약)

```bash
# 1) 데이터: AI Hub 71385 실내형분류기 다운로드(국내 IP) 후 변환
python3 training/convert_aihub_to_yolo.py <이미지경로> <라벨경로> <출력> 17

# 2) 학습 (RunPod A40 기준)
yolo detect train model=yolo11n.pt data=data.yaml epochs=60 imgsz=640 batch=128 workers=8

# 3) Jetson 배포
yolo export model=best.pt format=onnx opset=12
/usr/src/tensorrt/bin/trtexec --onnx=best.onnx --saveEngine=runB17.engine --fp16

# 4) 실행 (Jetson): inference/jetson_bridge.py — systemd 서비스로 자동 시작
```

※ 데이터셋(20GB)·API 키(.env.local)는 저장소에 포함하지 않음.
