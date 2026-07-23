// ============================================================
// 가정용 분리수거 자동분류기 — 실현가능성 우선 재설계 v1.0
// 단위: mm | OpenSCAD | 색상은 F5(미리보기)
//
// ★ 설계 철학: "만들 수 있는 것만 그린다"
//   - 기본 = 시연기(1개 투입). bulk_module=true 로 벌크 확장 표시
//   - MDF 12mm 판재 + 3D프린팅 소품 + 시판 원형통으로 제작 가능한 형태
//   - 과설계 제거: 외닫이 트랩도어(서보1), 게이트/리턴슈트 없음
//     (복수투입은 YOLO 박스 카운트 → 판별불가함으로 처리)
//
// ★ 코드 내장 검증 (F5 시 콘솔 echo 출력):
//   [1] 카메라 화각  [2] 유리 낙하고  [3] 2L 페트 수거함 수용
// ============================================================

bulk_module = false;   // ← true 로 바꾸면 호퍼+컨베이어 확장 표시
cutaway = false;       // ← true: 앞쪽 절반을 잘라 내부 구조 보기

$fn = 48;
t = 12;                                   // MDF 판 두께

// ---------------- 치수 (시연기 본체) ----------------
W = 580;  D = 420;  H = 880;              // 본체 외형
cx = 260; cy = D/2;                       // 캐러셀 축 (좌측 치우침)
orbit = 140;                              // 수거함 궤도 반경
drop_x = cx + orbit;                      // ★ 고정 수직 낙하선 x = 400
bin_r = 65; bin_h = 340;                  // 수거함 (2L: ⌀110×310 수용)
turn_r = orbit + bin_r + 8;               // 턴테이블 반경 = 213
turn_z = 60;                              // 턴테이블 상면 높이
stage_s = 260; stage_h = 240;             // 촬영 스테이지 내부
stage_z = turn_z + t + bin_h + 120;       // 수거함 상단 +120: 문짝 스윙(106)+낙하고(≤150) 동시 충족
cam_h  = stage_h - 20;                    // 카메라 높이(스테이지 바닥 기준)
inlet_w = 130; inlet_h = 140;             // 규격 투입구 (⌀110 페트 대응)

// ---------------- 검증 echo ----------------
fov_need = 2 * atan((stage_s/2) / cam_h);
echo(str("[검증1] 필요 화각 = ", round(fov_need), "° (PiCam3 수평 66° → ",
         fov_need < 66 ? "OK" : "부족! 스테이지 축소 필요", ")"));
echo(str("[검증2] 유리 낙하고(트랩도어→수거함 상단) = ", stage_z - (turn_z+t+bin_h),
         "mm (목표 ≤150 OK, 수거함 바닥 EVA폼 별도)"));
echo(str("[검증3] 수거함 내경 ", (bin_r-4)*2, "mm·깊이 ", bin_h,
         "mm vs 2L페트 ⌀110×310 → ", ((bin_r-4)*2>110 && bin_h>310) ? "OK" : "부족"));
door_drop = (stage_s/2) * sin(55);
clearance = stage_z - (turn_z + t + bin_h);
echo(str("[검증4] 양문 트랩도어 개방시 하강 ", round(door_drop), "mm vs 수거함까지 여유 ",
         clearance, "mm → ", door_drop < clearance ? "OK" : "충돌! 도어 축소 필요"));
echo(str("[치수] 스테이지 바닥 z=", stage_z, " / 본체 ", W, "x", D, "x", H));

// ---------------- 색상 ----------------
c_mdf   = [0.85, 0.78, 0.62];             // MDF
c_clear = [0.6, 0.75, 0.85, 0.55];                // 아크릴 창
c_mech  = [0.35, 0.45, 0.85];
c_bin   = [0.5, 0.75, 0.95];
c_rej   = [0.55, 0.55, 0.55];
c_dark  = [0.2, 0.2, 0.2];

// ============================================================
// 본체 캐비닛 (MDF 판재 구조 — 실제 재단 가능한 6면 구성)
// ============================================================
module cabinet_body() {
    color(c_mdf) {
        cube([W, t, H]);                                  // 뒷벽 (y=0)
        translate([0, D-t, 0]) difference() {             // 앞벽
            cube([W, t, H]);
            // 규격 투입구 (스테이지 정면, 낙하선 위치)
            translate([drop_x-inlet_w/2, -1, stage_z+40]) cube([inlet_w, t+2, inlet_h]);
            // 수거함 회수 도어 개구 (하부)
            translate([cx-170, -1, 40]) cube([340, t+2, bin_h+60]);
        }
        cube([t, D, H]);                                  // 좌벽
        translate([W-t, 0, 0]) cube([t, D, H]);           // 우벽
        translate([0, 0, H-t]) cube([W, D, t]);           // 천장
        cube([W, D, t]);                                  // 바닥
        difference() {                                    // 중판 (스테이지 받침)
            translate([0, 0, stage_z-t]) cube([W, D, t]);
            translate([drop_x-stage_s/2+10, cy-stage_s/2+10, stage_z-t-1])
                cube([stage_s-20, stage_s-20, t+2]);
        }
    }
    // 투입구 플랩 도어 (위 힌지, 반개방)
    color(c_mech, 0.9) translate([drop_x-inlet_w/2, D-t, stage_z+40+inlet_h])
        rotate([30, 0, 0]) translate([0, 0, -inlet_h]) cube([inlet_w, 4, inlet_h]);
    // 하부 회수 도어 (아크릴, 닫힌 상태) + 손잡이
    color(c_clear) translate([cx-170, D-2, 40]) cube([340, 4, bin_h+60]);
    color(c_dark)  translate([cx+130, D+2, 40+bin_h/2]) cube([8, 6, 60]);
    // 7" 디스플레이 (투입구 왼쪽) + 상태 LED 바
    color(c_dark)     translate([60, D-t+12.5, stage_z+30]) cube([160, 4, 110]);
    color([0.1,0.5,1]) translate([70, D-t+13, stage_z+40]) cube([140, 4.2, 90]);
    color([0.2,1,0.4]) translate([60, D-t+12.5, stage_z+150]) cube([160, 3, 6]);
    // 투입구 베젤 (3D프린팅 링, 규격 강조)
    color([0.9,0.55,0.15]) translate([drop_x-inlet_w/2-8, D-t+12, stage_z+32])
        difference() {
            cube([inlet_w+16, 5, inlet_h+16]);
            translate([8, -1, 8]) cube([inlet_w, 7, inlet_h]);
        }
    // 고무발 4개
    color(c_dark) for (p=[[30,30],[W-30,30],[30,D-30],[W-30,D-30]])
        translate([p[0], p[1], -14]) cylinder(r=18, h=14);
}

module cabinet() {
    if (cutaway) difference() {
        cabinet_body();
        translate([-50, cy+30, -30]) cube([W+700, D, H+100]);
    } else cabinet_body();
}

// ============================================================
// 캐러셀: 턴테이블(MDF 원판) + 시판 원형통 5개 + NEMA17 + 홀센서
//   i=0 칸이 낙하선(drop_x) 아래 정렬 상태
// ============================================================
module carousel() {
    color(c_mdf) translate([cx, cy, turn_z]) cylinder(r=turn_r, h=t);
    for (i = [0 : 4]) {
        ang = i * 72;
        col = (i == 4) ? c_rej : c_bin;
        color(col)
        translate([cx + orbit*cos(ang), cy + orbit*sin(ang), turn_z+t])
            difference() {
                cylinder(r=bin_r, h=bin_h);
                translate([0, 0, 4]) cylinder(r=bin_r-4, h=bin_h);
            }
    }
    // 구동: NEMA17 + GT2 벨트 감속 (토크 여유 확보)
    color(c_dark) {
        translate([cx, cy, turn_z-46]) cylinder(r=6, h=46);            // 축
        translate([cx+turn_r-60, cy+turn_r-40, t]) cube([42, 42, 40]); // NEMA17
    }
    color([0.9,0.1,0.1]) translate([cx+turn_r-12, cy-6, turn_z+t]) cube([10,10,4]);  // 마그넷
    color(c_dark) translate([cx+turn_r+4, cy-8, turn_z-10]) cube([8,16,24]);         // 홀센서
    // 턴테이블 가장자리 지지 롤러 3개 (중심축 단독 하중 방지 — 실제 필수 부품)
    color([0.4,0.4,0.45]) for (a=[30, 150, 270])
        translate([cx+(turn_r-22)*cos(a), cy+(turn_r-22)*sin(a), turn_z-16])
            cylinder(r=14, h=14);
    // GT2 감속: 센터 대풀리 + 모터 소풀리 + 벨트
    color(c_dark) translate([cx, cy, turn_z-28]) cylinder(r=34, h=10);
    color(c_dark) translate([cx+turn_r-39, cy+turn_r-19, t+40]) cylinder(r=10, h=10);
    color([0.1,0.1,0.1]) hull() {
        translate([cx, cy, turn_z-26]) cylinder(r=35, h=6);
        translate([cx+turn_r-39, cy+turn_r-19, turn_z-26]) cylinder(r=11, h=6);
    }
}

// ============================================================
// 촬영 스테이지 — 낙하선 위 오프셋. 외닫이 트랩도어(서보 1)
// ============================================================
module stage() {
    offx = drop_x - stage_s/2;
    offy = cy - stage_s/2;
    // 조명 박스 (내벽 무광백)
    color([0.97,0.97,0.95,0.75]) translate([offx, offy, stage_z])
        difference() {
            cube([stage_s, stage_s, stage_h]);
            translate([6,6,-1]) cube([stage_s-12, stage_s-12, stage_h-5]);
            translate([stage_s/2-inlet_w/2, stage_s-7, 30]) cube([inlet_w, 8, inlet_h]); // 투입 연결구
        }
    // 양문 트랩도어 (서보 2) — 외닫이는 개방시 수거함과 충돌(검증4)하여 기각
    color(c_mech) {
        translate([offx, cy, stage_z]) rotate([0, 55, 0])
            translate([0, -stage_s/2, -5]) cube([stage_s/2, stage_s, 5]);
        translate([offx+stage_s, cy, stage_z]) rotate([0, -55, 0])
            translate([-stage_s/2, -stage_s/2, -5]) cube([stage_s/2, stage_s, 5]);
    }
    color(c_dark) {   // MG996R ×2 (양측 힌지 구동)
        translate([offx-26, cy-13, stage_z-30]) cube([26, 26, 26]);
        translate([offx+stage_s, cy-13, stage_z-30]) cube([26, 26, 26]);
    }
    color([0.75,0.75,0.78]) for (xx=[offx-3, offx+stage_s-3])   // 경첩 4개
        for (yy=[cy-stage_s/2+30, cy+stage_s/2-30])
            translate([xx, yy-12, stage_z-6]) rotate([-90,0,0]) cylinder(r=6, h=24);
    // 상부: 카메라 + LED 링
    color(c_dark) translate([drop_x, cy, stage_z+stage_h]) cylinder(r=16, h=24);
    color([1,1,0.8]) translate([drop_x, cy, stage_z+stage_h-8])
        difference() { cylinder(r=55, h=5); translate([0,0,-1]) cylinder(r=44, h=7); }
    // 로드셀 (트랩도어 힌지측 하부 브래킷)
    color([0.75,0.75,0.78]) translate([offx-8, cy-40, stage_z-22]) cube([8, 80, 16]);
    // 카메라 화각 표시 (반투명 원뿔)
    color([1, 1, 0.4, 0.12]) translate([drop_x, cy, stage_z])
        cylinder(r1=stage_s/2*0.95, r2=8, h=cam_h);
}

// ============================================================
// 수직 드롭 가이드 (비산 방지 원통) + 전장부
// ============================================================
module drop_guide() {
    bin_top = turn_z + t + bin_h;
    color([0.98,0.85,0.3,0.6]) difference() {
        hull() {   // 깔때기: 입구는 열린 문짝까지 삼키는 폭, 출구는 수거함 안
            translate([drop_x, cy, stage_z-18]) cylinder(r=stage_s/2+6, h=4);
            translate([drop_x, cy, bin_top+4]) cylinder(r=bin_r+16, h=4);
        }
        hull() {
            translate([drop_x, cy, stage_z-19]) cylinder(r=stage_s/2, h=6);
            translate([drop_x, cy, bin_top+3]) cylinder(r=bin_r+10, h=6);
        }
    }
}

module electronics() {
    color([0.1,0.35,0.15]) translate([t+8, t+8, 200]) cube([90, 12, 50]);    // Pico 2 W
    color(c_dark)          translate([t+8, t+8, 270]) cube([60, 12, 40]);    // 드라이버
    color([0.7,0.7,0.72])  translate([t+8, t+8, 90])  cube([160, 40, 80]);   // 12V SMPS
}

// ============================================================
// (선택) 벌크 모듈: 호퍼 + 클리트 컨베이어 — 본체 우측 부착형
//   게이트·리턴 없음: 검증은 YOLO 카운트, 실패분은 판별불가함행
// ============================================================
module bulk_addon() {
    hw = 320; hd = 280; hh = 240;
    color([0.95,0.65,0.2]) translate([W+250, cy-hd/2, 0]) difference() {
        cube([hw, hd, hh]);
        translate([8,8,12]) cube([hw-16, hd-16, hh+1]);
    }
    translate([W-30, cy-100, H+40]) rotate([0, 55, 0]) {
        color([0.3,0.7,0.4]) cube([850, 200, 14]);
        for (x = [60 : 250 : 800])
            color([0.15,0.5,0.25]) translate([x, 10, 14]) cube([12, 180, 45]);
        color(c_dark) translate([840, -30, -8]) cube([50, 30, 30]);
    }
    color([0.9,0.4,0.35,0.8]) translate([drop_x-60, cy+40, H-10])
        rotate([28, 0, 0]) cube([120, 240, 6]);
}

// ============================================================
// 조립
// ============================================================
cabinet();
carousel();
stage();
drop_guide();
electronics();
if (bulk_module) bulk_addon();
