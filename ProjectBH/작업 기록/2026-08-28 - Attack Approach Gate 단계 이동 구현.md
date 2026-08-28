# 2026-08-28 - Attack Approach Gate 단계 이동 구현

> 롤백됨: PIE에서 Gate 주변 군중 병목이 심해져 [[2026-08-28 - Attack Approach Gate 롤백]]에서 관련 코드 전체를 제거했다. 이 문서는 폐기된 시도의 구현 이력으로만 유지한다.

> PIE 후속 결과: Gate와 Wait가 같은 Ring을 사용하고 Attack Orbit이 양방향이며 Crowd Separation도 꺼져 있어 Gate 주변 교착이 증가했다. 후속 완화 설계는 [[2026-08-28 - Gate 주변 군중 교착 완화 설계]]에 정리했다.

## 목적

Attack 예약자가 최종 슬롯으로 직행하면서 Attack/Wait Enemy 사이에 끼어 좌우로 진동하는 문제를 해결한다. Wait 슬롯 사이에 확보한 통로를 실제 Attack 이동 경로로 강제한다.

## Codex 변경 사항

### Approach Stage

`EBHCombatApproachStage`를 추가했다.

- `Direct`: Wait/Holding 등 일반 슬롯 직행
- `ApproachGate`: 현재 각도를 유지하며 Gate Ring 반경으로 이동
- `OrbitToGate`: Gate Ring 위에서 목표 Attack 각도까지 선회
- `Ingress`: Gate 각도 정렬 후 Attack Slot까지 방사형 진입
- `OrbitToSlot`: 기존 Combat Core 회피를 위한 일반 링 선회
- `AtSlot`: 예약 위치 도착

AI 디버그 문자열의 `Route` 항목은 이제 단순 `Direct/Orbit` 대신 이 Stage 이름을 표시한다.

### Attack Approach Gate

- 각 Attack Slot과 같은 각도의 Gate를 계산한다.
- `AttackApproachGateRadius`가 0이면 `WaitRingRadius`를 사용한다. 기본값은 0이다.
- Gate 반경은 Combat Core와 Attack Ring 안전 여유보다 작아지지 않도록 보정한다.
- Gate 위치는 다른 슬롯과 동일하게 NavMesh에 투영한다.
- Player 기준 Enemy 각도와 Attack Slot 각도의 차이가 기본 10도 이내일 때만 `Ingress`를 허용한다.
- 각도 정렬 전에는 최종 Attack Slot을 MoveTo 목표로 반환하지 않는다.
- Gate Ring 반경과 차이가 크면 현재 각도에서 Gate Ring으로 먼저 이동한다.
- Gate Ring에 도착하면 `OrbitWaypointAngleStep`만큼 목표 각도로 선회한다.
- 각도가 정렬된 뒤에는 위치가 Gate에서 멀어져도 다시 Gate로 돌아가지 않고 Attack Slot으로 계속 진입한다.

### Gate 경유 Admission 비용

- 초기 Attack 선정, 런타임 Wait 승격, 교착 중앙 교대가 모두 같은 Gate 경유 비용을 사용한다.
- 이미 Attack 각도에 정렬된 후보는 현재 위치에서 Attack Slot까지의 Nav 경로를 사용한다.
- 정렬되지 않은 후보는 다음 두 Nav 경로가 모두 완전해야 한다.
  1. 현재 위치 → Gate
  2. Gate → Attack Slot
- 후보 점수는 Nav 경로와 `Gate Ring 반경 정렬 + 링 호 길이` 중 더 큰 접근 비용에 Gate→Attack 비용을 더한다.
- 따라서 단순 직선으로는 가깝지만 Gate까지 크게 돌아야 하는 후열 후보가 Attack을 선점하기 어려워진다.

### 디버그 시각화

- Attack Gate를 청록색 작은 구체로 표시한다.
- Gate와 Attack Slot 사이의 방사형 Ingress 통로를 청록색 선으로 표시한다.
- Enemy 머리 위 `Route`에서 현재 이동 단계를 확인할 수 있다.

## 기본값

| 항목 | 값 | 의미 |
| --- | ---: | --- |
| Attack Approach Gate Radius | 0 cm | 0이면 Wait Ring Radius 사용 |
| Attack Approach Alignment Angle | 10도 | Ingress 허용 각도 오차 |
| Wait Ring Radius | 300 cm | Gate 기본 반경 |
| Orbit Ring Acceptance Radius | 35 cm | Gate Ring 도착 판정 여유 |
| Orbit Waypoint Angle Step | 45도 | 한 번에 선회하는 최대 각도 |

## 사용자 에디터 작업

별도 Blueprint 노드 연결은 없다. 에디터를 다시 열고 다음을 PIE에서 확인한다.

1. Attack Slot 바깥쪽에 청록색 Gate 네 개와 방사형 선이 보이는지 확인한다.
2. Attack 예약자 Route가 상황에 따라 `ApproachGate → OrbitToGate → Ingress → AtSlot`로 바뀌는지 확인한다.
3. `OrbitToGate` 상태의 Enemy가 Attack/Wait 사이로 직행하지 않고 외곽 Ring을 따라 움직이는지 확인한다.
4. `Ingress` 상태에서는 청록색 선을 따라 안쪽 Attack Slot으로 이동하는지 확인한다.
5. Enemy를 통로 주변에 밀집시켜 기존 좌우 진동이 감소했는지 확인한다.
6. 교착이 발생하면 2초 후 중앙 Attack↔Wait 교대가 정상 실행되는지 확인한다.

Hero Blueprint가 C++ 기본값을 덮어썼다면 Combat Engagement Slot Component에서 다음을 확인한다.

- `Wait Ring Angle Offset = 22.5`
- `Attack Approach Gate Radius = 0`
- `Attack Approach Alignment Angle = 10`

## 검증

- `git diff --check` 통과
- Unreal Header Tool 성공
- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 및 DLL 링크 성공
- 실제 Detour Crowd 통로 이동은 PIE 검증이 필요하다.

## 관련 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.h`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`
- `ProjectBH/기획/군중 몬스터 이동 및 전투 자리 배정 설계 초안.md`
- `ProjectBH/작업 기록/2026-08-28 - Attack 진입 Gate 필요성 분석.md`

## 남은 작업 / 다음 단계

- Gate 적용 후에도 Ingress 통로가 좁다면 `WaitRingRadius`를 325 cm와 350 cm로 비교한다.
- `OrbitToGate`에서 장시간 교착된다면 Stage별 Watchdog 시간을 분리한다.
- 플레이어가 빠르게 움직여 Gate와 Attack Slot이 이동할 때 Stage 전환이 안정적인지 검증한다.
