# 2026-08-28 - Attack 진입 Gate 필요성 분석

> 결론 변경: Gate 방식은 실제 군중에서 병목을 증가시켜 [[2026-08-28 - Attack Approach Gate 롤백]]에서 폐기했다.

## 목적

Attack Admission과 Wait Ring 22.5도 오프셋 적용 후에도 Attack 예약자가 Attack/Wait Enemy 사이에서 진동하며 전진하지 못하는 원인을 재분석한다.

## 확인 결과

- Wait Slot 위치는 Attack 방사선에서 비켜났지만 Attack 예약자의 실제 이동 경로는 그 방사선을 사용하도록 강제되지 않는다.
- `GetMoveGoalForReservedSlot`은 현재 위치에서 최종 Attack Slot까지의 선분이 Combat Core를 통과하지 않으면 최종 슬롯을 즉시 MoveTo 목표로 반환한다.
- 따라서 Attack Enemy는 Wait Ring의 빈 각도 통로로 정렬하지 않고 현재 위치에서 Attack Slot까지 최단 직선으로 파고든다.
- 초기·런타임 Admission에 사용하는 동기 Nav 경로 길이는 정적 NavMesh 경로다. 다른 Enemy의 현재 Capsule 배치와 Detour Crowd 혼잡 비용은 이 점수에 포함되지 않는다.
- 현재 2초 교착 교대는 막힌 Enemy를 다른 Wait 후보와 바꾸지만 새 후보도 같은 직선 경로를 선택할 수 있어 증상이 반복된다.

## 권장 해결책

Attack Slot마다 같은 각도의 전용 Approach Gate를 Wait Ring 반경에 둔다. Gate는 별도 점유 슬롯이 아니라 해당 Attack 예약자 한 명만 사용하는 계산된 경유점이다.

Attack 이동을 다음 단계로 분리한다.

1. `ApproachGate`: Attack Slot과 같은 각도의 Wait Ring 경유점으로 이동한다.
2. `OrbitToGate`: Gate로 가는 직선이 Combat Core를 통과하면 Wait Ring을 따라 선회한다.
3. `Ingress`: Gate 각도에 정렬되면 Gate에서 Attack Slot까지 방사형 통로로 진입한다.
4. `AtAttackSlot`: 최종 위치 도착 후 공격한다.

각도 정렬 여부는 Gate와의 단순 거리만 사용하면 안 된다. Gate에서 안쪽으로 이동한 직후 다시 Gate로 돌아가려는 문제가 생기기 때문이다. Player 기준 Requester 방향과 Attack Slot 방향의 각도 차이가 허용값 이내인지 판정해야 한다. 정렬된 뒤 안쪽으로 이동하는 동안 같은 단계가 유지된다.

## 추가 권장값

| 항목 | 초기값 | 의미 |
| --- | ---: | --- |
| Attack Approach Gate Radius | Wait Ring Radius 사용 | Attack 진입 전에 정렬할 외곽 반경 |
| Attack Approach Alignment Angle | 10도 | 방사형 Ingress를 허용하는 각도 오차 |
| Wait Ring Radius | 우선 300 cm 유지 | Gate 적용 후에도 여유가 부족할 때 325~350 cm 비교 |

## 구현 시 함께 바꿀 부분

- Attack Admission 경로 점수도 최종 Attack Slot 직행 거리가 아니라 `현재 위치 → Gate → Attack Slot` 합산 거리로 계산한다.
- AI 디버그 문자열의 `Route`를 단순 `Direct/Orbit`이 아니라 `Gate/Orbit/Ingress/Direct`로 구분한다.
- Stuck Watchdog은 현재 진입 단계를 함께 표시해 Gate 접근 교착과 Ingress 교착을 구분한다.
- Gate 적용 후에도 Detour Crowd가 통로를 좁게 판단하면 Wait Ring Radius를 325 또는 350 cm로 넓혀 비교한다.

## 권장하지 않는 대응

- Stuck Timeout만 줄이기: 교대 빈도만 증가하고 같은 직선 경로가 반복된다.
- Enemy Collision 끄기: 군중 간 공간 보존 기획을 훼손한다.
- Separation Weight만 높이기: 통로 정렬 없이 좌우 진동이 더 커질 수 있다.

## 관련 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.h`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`

## 현재 상태 / 다음 단계

- Attack Approach Gate, 단계별 MoveGoal, Gate 경유 Admission 점수, 단계 디버그는 [[2026-08-28 - Attack Approach Gate 단계 이동 구현]]에서 C++로 구현했다.
- 실제 통로 진입 품질과 Wait Ring 반지름 추가 조정 여부는 PIE 검증으로 결정한다.
