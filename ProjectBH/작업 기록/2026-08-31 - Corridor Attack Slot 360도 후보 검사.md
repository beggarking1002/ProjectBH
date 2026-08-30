# Corridor Attack Slot 360도 후보 검사

## 구현 범위

Corridor Attack Slot을 축 양쪽의 고정 원호 조합에서 360도 Ring 후보 집합으로 교체한다. 공유 후보는 공간 유효성으로 결정하고, 실제 Enemy의 완전한 경로는 해당 후보를 Enemy에게 배정하는 단계에서 검사한다. 공격·회복 중에는 현재 Attack Layout을 잠그며 Wait·Holding·Pending의 동적 Row 구현은 유지한다.

## 상태

- C++ 구현 완료
- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공
- PIE 검증 필요

## 구현 규칙

1. Player의 `Attack Ring Radius` 전체를 기본 `16개` 방향으로 균등 샘플링한다. Sample 0은 확정된 Corridor 축의 Rear 방향이며 이후 Sample은 같은 축을 기준으로 360도를 균등 분할한다.
2. 각 후보는 다음 조건을 모두 통과해야 한다.
   - NavMesh 투영 성공
   - 원래 후보와 투영점의 2D 오차 `35 cm 이하`
   - 투영된 Player 중심에서 후보까지 Navigation Raycast 통과
   - 후보 중심 주변 `8방향 × Enemy 보수 반경 45 cm`의 Nav 여유 확보
3. 유효 후보 중 투영 후 XY 좌표가 서로 `90 cm` 이상 떨어진 조합을 최대 Attack Slot 수 `5개`까지 선택한다.
4. 조합 비교 우선순위는 `활성 수 최대 → Corridor 축 정렬도 최대 → Side 수 차이 최소 → 기존 Sample 유지 최대`다.
5. 초기 Attack 배정과 전열 재편은 Enemy에서 후보까지 유효하고 Partial이 아닌 동기 Nav 경로가 있는 경우만 허용한다. Wait 승격도 기존 도착·Cooldown 조건과 함께 같은 경로 조건을 검사한다. 경로가 없는 Enemy는 다른 유효 후보를 검사하고, 어느 후보에도 갈 수 없으면 Wait·Holding·Pending Queue에 남는다.
6. 새 후보 조합이 `0.3초` 동안 연속으로 같아야 한 번에 확정한다. 여기서 같은 조합은 정렬된 Sample Index 목록이 같다는 뜻이다. 목록이 달라지면 안정화 시간을 처음부터 다시 센다.
7. 한 명이라도 공격 또는 회복 중이면 Attack 후보 조합 전체를 잠근다. Player를 따라 각 Sample의 월드 좌표는 계속 갱신하지만 Sample Index 목록과 잠긴 Enemy의 기존 Slot Index는 유지한다.

## 디버그

- `bh.Debug.Slots 1`
- 어두운 빨강 작은 구체: 검사 탈락 후보
- 흰색 작은 구체: 유효하지만 최종 미선택 후보
- 초록 Attack 구체: 선택됐고 비어 있는 Slot
- 빨강 Attack 구체: 선택됐고 점유된 Slot
- 공간 문자열 `AttackCand:유효/전체`는 공간 검사를 통과한 후보 수, `Selected`는 최종 조합 수, `ActiveA`는 현재 활성 Attack Slot 수다.

## 수치 조정 위치

`Combat Engagement Slot Component`의 Class Defaults에서 `Combat | Corridor Formation` 범주를 연다.

- `Corridor Attack Candidate Count`: 전체 Ring 샘플 수, 기본 `16`, 허용 `8~32`
- `Corridor Attack Minimum Spacing`: 선택 후보 간 최소 거리, 기본 `90 cm`
- `Corridor Attack Clearance Probe Count`: 후보 주변 여유 탐침 수, 기본 `8`
- `Corridor Attack Projection Tolerance`: NavMesh 투영 허용 오차, 기본 `35 cm`
- `Corridor Capacity Commit Delay`: 새 후보 조합 안정화 시간, 기본 `0.3초`

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`
- `ProjectBH/기획/몬스터 이동 시스템 조작 및 수치 치트시트.md`

## PIE 확인 항목

1. 통로 중앙에서는 후보 16개 중 벽 쪽 후보가 탈락하고 축 앞뒤 후보가 우선 선택되는지 확인한다.
2. Player가 통로 측면에 붙었을 때 실제 여유 공간이 있는 측면·후방 후보가 흰색 또는 선택 Slot으로 살아나는지 확인한다.
3. Enemy가 해당 후보까지 완전한 경로가 없으면 초기 Attack 예약과 재편에서 그 후보를 받지 않는지 확인한다.
4. 공격 Montage 재생 중 Player가 움직여도 공격 중인 Enemy의 Slot Index와 후보 조합이 바뀌지 않는지 확인한다.
5. 공격·회복이 모두 끝난 뒤 새 후보 조합이 약 `0.3초` 안정화 후 반영되는지 확인한다.

디버그 표시는 같은 알고리즘이 출력한 자기 보고이므로 그것만으로 성공을 판정하지 않는다. 벽이 보이는 고정 테스트 지형, Enemy의 실제 이동·도착 여부, Montage가 끊기지 않는 장면을 함께 녹화해 대조한다. 완전 경로 검사는 출입구가 없는 분리된 NavMesh 구역처럼 결과를 사전에 알 수 있는 실패 사례를 포함한다.

## 후속 진행 상태

- Wait Enemy의 반대 Side Attack 승격과 Combat Core 동적 좌우 우회를 후속 4단계에서 구현했다.
- 상세 규칙과 PIE 확인 항목은 [[2026-08-31 - Corridor 동적 Side 전환과 Combat Core 우회]]를 따른다.
