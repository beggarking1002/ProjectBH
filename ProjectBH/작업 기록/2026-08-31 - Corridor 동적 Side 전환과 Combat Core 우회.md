# Corridor 동적 Side 전환과 Combat Core 우회

## 구현 범위

Corridor의 360도 Attack 후보는 유지한다. 초기 배정·Wait 승격·전열 재편에서 기존 Side와 Lane을 금지 조건이 아니라 안정화 선호값으로 낮춰, 반대편 후보라도 실제 완전 경로가 있으면 점유할 수 있게 한다. Enemy에서 Slot까지의 직선이 Player 중심 Combat Core를 가로지르면 고정 Gate를 만들지 않고, Player 주위 우회 Ring의 양방향 동적 waypoint를 NavMesh와 경로 점수로 비교한다. 선택한 회전 방향은 해당 예약을 이동하는 동안 유지하고 막혔을 때만 반대 방향으로 전환한다.

## 상태

- C++ 구현 완료
- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공
- PIE 검증 필요

## Side 전환 규칙

`Side`는 확정된 Corridor 축을 기준으로 나눈 두 반공간이며 Queue Entry에 안정값으로 저장된다. Wait·Holding의 `Lane`은 동적 Row 안의 실제 횡방향 자리다. Attack `채널`은 기존 배치를 덜 흔들기 위해 `Side`와 Attack Sample에서 파생한 선호 버킷이며, Lane과 같은 영구 소유권이 아니다.

1. 초기 Attack 배정은 Queue Sequence가 가장 빠른 도달 가능 Enemy를 먼저 선택한다.
2. 같은 Enemy에게 여러 Attack 후보가 있으면 `기존 Side → 기존 채널 → 경로 점수` 순으로 선호한다.
3. Wait 승격은 전체 빈 Attack Slot에서 같은 Side 후보를 먼저 찾는다. 여기서 후보는 Wait 도착, Cooldown, 공격 거리, 비Partial 완전 경로 조건을 모두 통과한 Enemy와 빈 Slot의 조합이다. 이런 같은 Side 조합이 하나도 없을 때만 반대 Side까지 검사한다.
4. 전열 재편과 직접 Attack 예약도 같은 Side·채널을 먼저 사용하지만 반대 Side를 금지하지 않는다.
5. 모든 Attack 배정은 비Partial 완전 Nav 경로를 요구한다. 반대 Side Slot이 확정되면 Queue Entry의 안정 Side를 새 Attack Side로 갱신한다.

## Combat Core 우회 규칙

1. Enemy에서 최종 Slot까지의 XY 선분이 Player 중심 Combat Core를 가로지르지 않으면 기존처럼 `Direct`로 이동한다.
2. 가로지르면 `max(Combat Core + Padding, Attack Ring + Orbit Acceptance)` 반경의 우회 Ring을 사용한다. 기본값은 `max(100 + 45, 125 + 35) = 160 cm`다.
3. Enemy가 Ring에서 `35 cm`보다 벗어났으면 현재 방사 방향의 Ring 지점으로 먼저 이동한다.
4. Ring 위에서는 현재 각도에서 양·음 회전 방향으로 최대 `45도` 앞의 waypoint를 각각 만든다.
5. waypoint는 투영 오차 `35 cm 이하`, Enemy→waypoint 완전 경로, waypoint→최종 Slot 완전 경로를 모두 만족해야 한다. 실제 Enemy→waypoint Nav 경로를 이루는 모든 선분도 Combat Core를 침범하면 안 된다.
6. 양방향이 유효하면 `두 경로 점수 + 남은 원호 길이`가 낮은 방향을 선택한다. 각 경로 점수는 `Nav 경로 길이 + 경로 110 cm 안의 다른 Enemy 수 × 200`이다.
7. 점수가 같으면 Queue Sequence 홀짝으로 방향을 분산한다.
8. 선택 방향은 `BypassCorePositive` 또는 `BypassCoreNegative`에 저장한다. 다음 waypoint의 투영 또는 비Partial 경로 검사가 실패할 때만 반대 방향을 검사한다. 실제 MoveTo 요청 실패나 교착 Watchdog은 기존 예약 실패·재등록 흐름을 사용한다.
9. 우회를 시작한 뒤에는 최종 Slot 방향과의 각도 차이가 `10도 이하`이고 실제 Nav 경로의 모든 선분이 Combat Core를 피할 때만 `Direct`로 전환한다. 이 진출 조건으로 Core 접선 부근의 `Direct ↔ Bypass` 진동을 막는다.
10. Player 이동 때마다 Ring 중심과 waypoint를 현재 Player 위치로 다시 계산한다. Slot 또는 예약이 바뀌면 Route Stage를 `Direct`로 초기화하고 새 경로에서 방향을 다시 선택한다. 양방향이 모두 무효면 현재 예약을 실패 처리하고 Queue 위치를 보존해 다시 배정받는다.

## 디버그

- `bh.Debug.Slots 1`
- 주황색 원: Combat Core
- 얇은 보라색 원: Corridor 동적 우회 Ring
- 자홍색 Move Goal 선: `BypassCorePositive`
- 보라색 Move Goal 선: `BypassCoreNegative`
- Enemy 상태 문자열의 `Route`에서 현재 Route Stage를 확인한다.

## 수치 조정 위치

`Combat Engagement Slot Component`의 Class Defaults에서 `Combat | Approach Routing` 범주를 연다.

- `Combat Core Radius`: 기본 `100 cm`
- `Combat Core Bypass Padding`: 기본 `45 cm`
- `Combat Core Bypass Projection Tolerance`: 기본 `35 cm`
- `Orbit Waypoint Angle Step`: 기본 `45도`
- `Orbit Ring Acceptance Radius`: 기본 `35 cm`

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`
- `ProjectBH/기획/몬스터 이동 시스템 조작 및 수치 치트시트.md`

## PIE 확인 항목

1. 한쪽 Wait에만 Enemy가 있고 반대쪽 Attack Slot만 비어 있는 상태에서 반대 Side 승격이 발생하는지 확인한다.
2. 승격된 Enemy의 상태 문자열 `Side`가 새 Attack Slot Side로 바뀌는지 확인한다.
3. 이동 직선이 Combat Core를 가로지를 때 `Route`가 `ApproachRing`을 거쳐 `BypassCorePositive` 또는 `BypassCoreNegative`가 되는지 확인한다.
4. 우회 중 Move Goal 선이 매 갱신마다 반대 색으로 뒤집히지 않는지 확인한다.
5. 한쪽 우회 waypoint를 벽으로 막으면 유효한 반대 방향을 선택하는지 확인한다.
6. 최종 Slot에 가까워져 직선이 Combat Core를 벗어나면 `Route:Direct`로 바뀌고 공격까지 이어지는지 확인한다.
7. 12마리에서 `stat game`, `stat ai`를 확인하고 우회 중 프레임 급락이나 반복적인 Move 실패 로그가 없는지 확인한다.
8. 초기 대형 형성과 Attack 후보 재편에서도 같은 Side 후보가 없을 때 반대편의 도달 가능한 Enemy가 빈 Slot을 채우는지 확인한다.
9. Player가 우회 중 이동해도 Route가 매 갱신마다 `Direct`와 Bypass를 왕복하지 않고, 새 Slot을 배정받았을 때만 방향을 다시 선택하는지 확인한다.
10. 같은 Side에서 직접 접근 가능한 기존 장면에서는 불필요한 우회나 Side 전환이 발생하지 않는지 회귀 확인한다.
11. 양쪽 waypoint가 모두 무효인 장면에서는 Enemy가 예약을 영구 점유하지 않고 Queue 우선순위를 보존해 재배정되는지 확인한다.

디버그 문자열과 선은 구현 코드가 출력하는 자기 보고이므로 단독 합격 근거로 사용하지 않는다. Side 전환은 Enemy가 실제 반대편 Slot에 도착해 공격하는 장면으로, Core 우회는 Enemy Transform 궤적이 주황색 Core 내부를 관통하지 않는 장면으로 함께 확인한다. 벽 차단 검사는 `P`로 표시한 NavMesh에서 한쪽 우회 구간을 의도적으로 끊은 고정 지형을 사용한다. 성능은 같은 12마리 배치에서 교차 이동이 없는 30초와 강제 반대 Side 이동 30초를 각각 Unreal Insights로 기록해 Game Thread와 Nav 경로 조회 비용을 비교한다.

## 비포함 범위

- Behavior Tree 도입
- Detour Crowd 설정 변경
- Pocket 대형의 별도 우회 정책
- 새로운 고정 Gate Actor 또는 Blueprint 작업
