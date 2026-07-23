#!/bin/bash
# Jetson 촬영·판정 실행 후 결과 이미지를 Windows 폴더로 복사
# (바탕화면 촬영판정.bat 이 호출)
JET=jetson@192.168.55.1
OPT="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10"
OUT="/mnt/c/Users/wjswp/OneDrive/Desktop/분류테스트/결과/jetson_result.jpg"

ssh $OPT $JET 'cd ~ && python3 jetson_infer.py' 2>/dev/null | grep -v "WARN:0"
scp $OPT -q $JET:~/result.jpg "$OUT" 2>/dev/null && echo "결과 이미지 저장됨"
