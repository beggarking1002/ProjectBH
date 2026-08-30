# Corridor Side별 유효 수용량 구현

> 후속 결정: Corridor 변에서는 이 Attack 배치를 확장하지 않고 Pocket으로 전환한다. 폭 축 Attack 후보와 Cross-Channel 승격 실험은 롤백했다. 이 문서의 Side별 수용량은 Corridor 중앙부에만 적용된다.

## 작업 목적

- Player가 Corridor 중앙이 아니라 통로 끝이나 벽 가까이에 있을 때 막힌 Side의 Attack Slot이 그대로 남는 문제를 줄인다.
- 통로 중앙부에서 각 긴 축 Side의 실제 NavMesh 유효성을 반영한다.
- Open, Pocket, Corridor Wait·Holding·Pending Queue 규칙은 유지한다.

## Side별 수용량 판정

각 Side에서 `1개`부터 폭 기준 최대 수까지 가능한 모든 전열을 각각 별도로 만들고 다음 조건을 검사한다.

1. 후보 Attack 위치를 NavMesh에 투영할 수 있다.
2. 후보점과 투영점의 2D 차이가 `CorridorAttackProjectionTolerance = 35 cm` 이하다.
3. NavMesh에 투영한 Player 위치에서 후보 투영점까지 Navigation Raycast가 막히지 않는다.
4. 투영된 후보 중심끼리 최소 `90 cm` 간격을 유지한다.

양쪽에서 유효한 정확한 개수 조합을 모두 비교해 `총 Slot 수 최대`, `양쪽 불균형 최소`, `현재 배치 변경 최소` 순으로 최종 조합을 선택한다. 한쪽이 막히면 `3/2` 고정이 아니라 `4/1`, `3/0`, `1/4` 같은 배치가 가능하며 양쪽 모두 실제로 불가능하면 `0/0`도 허용한다.

## Slot 인덱스와 Queue Channel

- 활성 Attack Slot은 Side별 수를 기준으로 다시 인덱싱한다.
- 양쪽이 모두 있으면 기존처럼 `Side 0`, `Side 1`을 번갈아 배치한다.
- 한쪽이 0이면 모든 활성 Attack 인덱스가 유효한 반대 Side를 가리킨다.
- Attack Channel은 `Side × Lane + Side 내부 Slot 순번 % Lane 수`로 유지한다.
- 기존 Enemy Side 소유권은 유지하며 반대 Side Queue의 자동 Spillover는 발생하지 않는다.

## 안정화와 공격 잠금

- 새 Side 조합이 `CorridorCapacityCommitDelay = 0.3초` 동안 연속 유지된 뒤에만 실제 예약을 재편한다.
- Nav 시스템 또는 Player 원점 투영 조회가 일시적으로 불가능하면 기존 확정 배치를 유지한다.
- Attacking·Recovering Enemy가 한 명이라도 있으면 현재 Attack 배치 전체를 잠근다. 잠금 중에는 Slot 인덱스나 Side가 재해석되지 않는다.
- 모든 잠금이 끝난 뒤 안정화 시간을 통과한 Nav 유효 조합으로 재편한다.
- 같은 Side 안에서는 원래 Lane을 우선하되, 그 Lane에 자리가 없으면 같은 Side의 다른 Lane을 사용할 수 있다. 반대 Side로는 이동하지 않는다.

## 디버그

- `bh.Debug.Slots 1`의 공간 문자열에 `ActiveA:총합 (Side0/Side1)`을 표시한다.
- 예: `ActiveA:4 (1/3)`은 Side 0에 1개, Side 1에 3개가 활성이라는 뜻이다.

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`
- `ProjectBH/기획/몬스터 이동 시스템 조작 및 수치 치트시트.md`

## 빌드 검증

- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공
- UnrealHeaderTool, C++ 컴파일, DLL 링크 성공
- Visual Studio 2022 컴파일러가 엔진의 선호 버전이 아니라는 기존 경고만 발생했다.

## 독립 검토 반영

- 최초 구현의 단일 `최대 수용량` 방식은 Slot 수 변화 때 원호 각도가 다시 중앙 정렬되는 점을 보장하지 못해, 정확한 개수별 유효 집합 방식으로 교체했다.
- Nav 조회 실패 시 임의의 `1/0` 배치를 만드는 동작을 제거하고 마지막 확정 배치를 유지하도록 했다.
- 공격·회복 잠금 수만큼 검증되지 않은 새 Slot을 만드는 대신, 잠금이 하나라도 있으면 확정 배치 전체를 유지하도록 했다.
- 실제 예약 배열 크기보다 많은 논리 Slot을 만들지 않으며, 실제 유효한 Attack 후보가 없으면 `0/0`을 확정할 수 있다.

## PIE 검증

1. 에디터를 재시작한다.
2. `bh.Debug.Enabled 1`, `bh.Debug.Slots 1`, `bh.Debug.Crowd 1`을 실행한다.
3. Corridor 중앙에서 `ActiveA:5 (3/2)` 또는 폭에 맞는 균형 배치가 유지되는지 확인한다.
4. 한쪽 끝·벽으로 이동해 막힌 Side 수가 감소하고 열린 Side 수가 유지되는지 확인한다.
5. 초록색·빨간색 Attack Slot이 NavMesh 밖이나 같은 위치에 겹치지 않는지 확인한다.
6. 공격 중 벽으로 이동해도 Montage가 강제 중단되지 않는지 확인한다.
7. 벽 경계에서 잠깐 움직였을 때 `ActiveA` 조합이 매 Tick 왕복하지 않고 약 `0.3초` 뒤 확정되는지 확인한다.
8. Player가 Corridor 변으로 이동하면 이 배치를 유지하지 않고 Pocket으로 전환되는지 확인한다.
