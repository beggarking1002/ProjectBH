# Open 밀도 및 Pocket 부채꼴 대형 구현

> 2026-08-30부터 전체 Attack Slot 상한은 4개에서 5개로 변경됐다. 현행 규칙은 [[2026-08-30 - Attack Slot 5개 및 양방향 Corridor 구현]]을 따른다.

## 작업 목적

- 넓은 공간의 Wait·Holding·Pending Slot이 지나치게 멀어 보이는 문제를 완화한다.
- 벽 또는 구석을 등진 Player 앞에서 Corridor 종대가 한 줄로만 서고 넓은 이동 가능 공간을 비우는 문제를 해결한다.
- 진짜 좁은 통로의 Corridor Lane Queue는 유지한다.

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`

## Open 기본 반경

| 계층 | 이전 | 현재 |
| --- | ---: | ---: |
| Attack | 125 cm | 125 cm |
| Wait | 300 cm | 250 cm |
| Holding | 500 cm | 400 cm |
| Pending | 700 cm | 575 cm |

- Attack은 공격 시작 거리와 연결되므로 유지했다.
- Wait·Holding·Pending만 줄여 외곽 대형의 시각적 밀도를 높였다.
- 기존 Hero Blueprint가 값을 명시적으로 Override했다면 Component Details에서 새 값으로 바꾸거나 노란색 Reset 화살표를 눌러야 한다.

## Pocket 공간 판정

- `EBHCombatSpaceMode`에 `Pocket`을 추가했다.
- 가까운 벽 탐침 비율과 긴 연속 Open Arc를 함께 사용한다.
- 진입 기본값:
  - Probe Clearance: 350 cm
  - Nearby Wall Distance: 200 cm
  - Blocked Fraction: 0.2
  - Minimum Open Arc: 90도
  - 지속시간: 0.4초
- 이탈 기본값:
  - Blocked Fraction: 0.1
  - Minimum Open Arc: 67.5도
  - 지속시간: 0.6초
- 좁은 통로는 열린 탐침이 전후의 작은 묶음으로 분리되므로 Pocket이 아니라 Corridor를 유지한다.

## Pocket 슬롯 규칙

- 가장 긴 Open Arc 중앙을 Formation 방향으로 삼는다.
- 최대 반각 80도의 부채꼴에 Slot을 배치한다.
- 호 중심 간격은 95 cm다.
- 한 Row 용량을 넘으면 반경을 100 cm 늘린 다음 Row에 배치한다.
- 벽에 막힌 후보는 부채꼴 중앙 쪽으로 각도를 줄여 다시 Navigation Raycast한다.
- Pocket은 전체 원 Orbit 경유를 사용하지 않는다.
- Attack은 Open과 같은 4개까지 사용한다.
- Corridor Lane Queue는 Pocket에서 사용하지 않지만 중앙 Queue와 승격·반납 조건은 유지한다.

## 디버그

- `bh.Debug.Slots 1`에서 Pocket 열린 방향은 주황색 화살표다.
- Player 위 공간 문자열에 다음 값이 추가됐다.
  - `PocketArc`: 연속해서 열린 각도
  - `Wall`: 가까운 벽 탐침 비율
  - 현재/후보 상태 `Open`, `Corridor`, `Pocket`

## 빌드

- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공.
- PIE 동작 검증은 에디터에서 진행해야 한다.

## PIE 검증 순서

1. 에디터를 재시작하고 `bh.Debug.Slots 1`을 실행한다.
2. 넓은 평지에서 `Open`과 `125 / 250 / 400 / 575 cm` 밀도를 확인한다.
3. 좁은 통로 중앙에서 `Corridor`와 기존 1~2 Lane 종대를 확인한다.
4. 넓은 방의 벽을 등지면 약 0.4초 후 `Pocket`으로 전환되는지 확인한다.
5. 구석에서도 주황색 화살표가 열린 공간을 향하고 Enemy가 전방 부채꼴을 채우는지 확인한다.
6. Pocket에서 Player가 움직이거나 회전해도 Attack 중 Montage가 끊기지 않는지 확인한다.
7. 벽 경계에 여러 Slot이 같은 위치로 겹치거나 Enemy가 벽 쪽 목적지를 반복 요청하지 않는지 확인한다.

## 기준 문서

- [[몬스터 이동 시스템 규칙]] 36.9절
