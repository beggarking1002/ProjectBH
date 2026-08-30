# Pocket Attack 동적 수용량 구현

> 이해한 작업 범위: 앞선 진단의 수정 우선순위 1번인 Pocket Attack Slot의 단일 Row와 동적 활성 수를 구현한다. Corridor Side 재배분, NavMesh 레벨 수정, Pocket Ingress Waypoint는 이번 작업에 포함하지 않는다.

## 작업 목적

- 좁은 Pocket 부채꼴에서 5번째 Attack Slot이 `225 cm`의 두 번째 Row로 밀려 공격 거리 `150 cm`를 위반하는 문제를 제거한다.
- 공간에 실제로 들어가는 Attack Slot만 활성으로 표시하고 예약한다.
- Open과 Corridor의 기존 배치 규칙은 유지한다.

## 구현 내용

### 동적 활성 Attack 수

- 감지된 Pocket Open Arc와 `PocketMaximumArcHalfAngle` 중 작은 범위를 사용한다.
- `AttackRingRadius = 125 cm`에서 `PocketSlotSpacing = 95 cm`를 만족하는 각 간격을 계산한다.
- 첫 Row에 들어가는 수만 `ActivePocketAttackSlotCount`로 사용한다.
- 전체 시스템 상한은 5개다. 다만 현재 `125 cm` 반경, `95 cm` 간격, 최대 반각 `80도`에서는 정상 Pocket 수용량이 최대 4개다.

### Attack 단일 Row

- Pocket Attack은 항상 Player 중심 반경 `125 cm`에 배치한다.
- Attack에는 `PocketRowSpacing = 100 cm`를 적용하지 않는다.
- Wait·Holding·Pending은 기존처럼 공간이 부족하면 다음 방사형 Row를 사용한다.

### 예약 재편

- Pocket 진입 또는 열린 호 변화로 활성 수가 달라지면 기존 Attack 예약을 가까운 유효 Slot로 다시 배치한다.
- 수용량을 초과한 비잠금 Attack Enemy는 Wait, Holding, Pending 순으로 강등한다.
- Attacking·Recovering Enemy는 Montage를 끊지 않기 위해 잠금 수만큼 임시 활성 수를 유지한다.
- 잠금이 끝나면 계산된 목표 수용량으로 축소한다.
- 새 활성 전열의 모든 위치를 NavMesh에 투영한 뒤에만 예약 재편을 확정한다. 투영이 실패하면 이전 활성 수와 Attack 예약을 유지하고 다음 Tick에서 다시 시도한다.

### 디버그 결과

- 좁은 Pocket이 4명만 수용하면 `A:4/4`와 `ActiveA:4`로 표시해야 한다.
- 더 좁은 Pocket이 3명만 수용하면 `A:3/3`과 `ActiveA:3`으로 표시해야 한다.
- 비활성 Attack Slot은 그리지 않고 예약 후보에서도 제외한다.

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`

## 빌드 검증

- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공
- UnrealHeaderTool, C++ 컴파일, DLL 링크 성공
- Visual Studio 2022 컴파일러가 엔진의 선호 버전이 아니라는 기존 경고만 발생했다.

## PIE 검증

1. 에디터를 재시작한다.
2. `bh.Debug.Enabled 1`, `bh.Debug.Slots 1`, `bh.Debug.Crowd 1`을 실행한다.
3. Open에서 Attack Slot 분모가 계속 5인지 확인한다.
4. Pocket에 진입해 Open Arc가 좁아지면 분모가 3~4로 줄어드는지 확인한다.
5. 초록색 Attack Slot이 `125 cm` 전열 한 줄에만 그려지는지 확인한다.
6. `A:n/n` 상태에서 예약 Enemy가 각 Slot에 도착하고 공격하는지 확인한다.
7. 공격 중 Open ↔ Pocket 전환을 반복해 Montage가 중단되지 않는지 확인한다.
8. Wait·Holding·Pending의 여러 Row는 그대로 유지되는지 확인한다.
