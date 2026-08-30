# Attack Slot 5개 및 양방향 Corridor 구현

## 작업 목적

- Open과 Pocket의 최대 Attack Slot을 4개에서 5개로 늘린다.
- Corridor에서 Player 한쪽에만 생성되던 전투 대형을 통로 축 양쪽에 배치한다.
- Enemy가 반대편 빈자리를 차지하려고 Player를 가로지르는 현상은 허용하지 않는다.

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`

## Attack Slot 총량

- `AttackSlotCount` 기본값을 `4 → 5`로 변경했다.
- Open과 Pocket에서는 유효 경로와 공격 거리 조건을 만족하면 최대 5명이 Attack Slot을 점유한다.
- 전체 예약 가능 수는 `Attack 5 + Wait 8 + Holding 16 = 29명`이다.
- 30번째부터 Pending Queue를 사용한다.

## Corridor 양방향 구조

### Side 결정

- Corridor 진입 시 각 Enemy 위치를 감지된 통로 축에 투영한다.
- Player 기준 축의 기준 방향은 `Side 0`, 반대 방향은 `Side 1`이다.
- Player와 축 위치가 거의 같은 Enemy는 현재 인원이 적은 Side로 배정한다.
- Corridor가 유지되는 동안 Side를 고정한다.
- Corridor를 나갔다가 다시 들어오면 현재 위치로 Side를 다시 계산한다.

### Queue Channel

- 하나의 독립 Queue Channel은 `Side × Lane`이다.
- Side는 2개, Lane은 통로 폭에 따라 Side당 1~2개다.
- Wait·Holding·Pending은 `Row * ChannelCount + ChannelIndex`로 배치한다.
- 같은 Channel 안에서만 뒤 Enemy가 앞으로 압축된다.
- 다른 Side의 빈자리 때문에 Enemy가 Player를 가로질러 승격하지 않는다.

### Attack 배치

- Attack Slot은 Player 양쪽의 `AttackRingRadius = 125 cm` 원호에 배치한다.
- 통로 폭으로 계산한 Side당 수용량을 두 배 한 뒤 전체 Attack Slot 5개로 제한한다.
- 정상 활성 Attack 수는 폭에 따라 양쪽 합계 `2~5개`다.
- 5개가 모두 활성화되면 `Side 0 = 3개`, `Side 1 = 2개`다.
- 같은 Attack Channel의 Wait 선두를 우선하고, 없으면 같은 Side의 다른 Lane 선두까지만 허용한다.

### 공격 잠금

- 공간 전환 순간 Attacking 또는 Recovering인 Enemy 수가 정상 Corridor 수용량보다 많아도 Montage를 강제로 끊지 않는다.
- 잠금 종료 후 정상 활성 Attack 수까지 순차적으로 축소한다.

## 디버그

- `bh.Debug.Slots 1`에서 Corridor 축 양쪽에 보라색 화살표를 표시한다.
- 공간 문자열은 `Sides:2`, Side당 `Lanes`, 양쪽 합계 `ActiveA`를 표시한다.
- Enemy 머리 위 문자열에 `Side`와 `Lane`을 별도로 표시한다.

## 빌드

- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공.
- PIE 검증은 에디터에서 진행해야 한다.

## PIE 검증 순서

1. 에디터를 재시작하고 `bh.Debug.Enabled 1`, `bh.Debug.Slots 1`, `bh.Debug.Crowd 1`을 실행한다.
2. Open에서 Enemy 5명이 서로 다른 Attack Slot을 점유하는지 확인한다.
3. 좁은 통로의 Player 양쪽에 Enemy를 배치하고 `Side 0/1`이 실제 위치에 맞게 고정되는지 확인한다.
4. 빨간 Attack Slot과 Wait·Holding·Pending 종대가 Player 양쪽에 생성되는지 확인한다.
5. 한쪽 Attack Enemy를 죽였을 때 같은 Side의 Wait 선두가 승격되는지 확인한다.
6. 한쪽 Side에 후보가 없을 때 반대 Side Enemy가 Player를 횡단하지 않고 해당 Attack Slot이 비어 있는지 확인한다.
7. Corridor에서 Open 또는 Pocket으로 나간 뒤 최대 Attack Slot이 5개로 복귀하는지 확인한다.
8. 공격 또는 회복 중 공간 상태가 바뀌어도 Montage가 강제 중단되지 않는지 확인한다.

## 기준 문서

- [[몬스터 이동 시스템 규칙]] 8.1, 28.2, 36.7~36.9절
