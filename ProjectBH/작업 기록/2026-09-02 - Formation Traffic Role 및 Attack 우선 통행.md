# Formation Traffic Role 및 Attack 우선 통행

## 문제

거리 기반 Reserve Layer 배정으로 가까운 Enemy가 Wait에 직행하게 되었지만, Attack 진입자와 Wait 진입자, 정지 Holding이 같은 좁은 공간을 동시에 사용했다. NavMesh 경로는 모두 유효해도 Detour Crowd는 시간 순서를 정하지 않으므로 대칭 양보와 병목이 발생했다.

## 이번 범위

1. Formation 이동 역할 구분
2. Attack 이동 통로와 겹치는 새 Wait 출발 보류
3. 실제 교착 경로를 막는 Stationary Holding 한 명의 임시 양보

Side별 동시 진입 제한과 경로 교차 비용은 이번 범위에서 제외했다.

## 규칙

- Traffic Role은 `AttackIngress`, `WaitIngress`, `WaitIngressDeferred`, `HoldingTransit`, `StationaryHolding`, `HoldingYield`, `PendingTransit`으로 구분한다.
- `Chasing` 상태이며 Attack Slot에서 `55 cm`보다 먼 Attack Owner만 진입 우선권을 가진다.
- Wait의 다음 이동 경로와 Attack 최종 Slot 경로의 선분 간 거리를 비교한다.
- 충돌 반경은 두 Enemy Capsule 반지름 합에 `20 cm`를 더한 값이다.
- 높이 차이 `100 cm`를 초과하는 경로 선분은 다른 층으로 제외한다.
- 충돌한 Wait는 Slot을 반납하지 않고 출발만 보류하며 `0.2초`마다 재검사한다.
- 이미 이동 중인 Wait는 중간에 멈추지 않고, 새 이동 구간을 시작할 때만 검사한다.
- Attack·Wait 이동자가 `0.75초` 동안 Move Goal에 가까워지지 못한 경우에만 전방 `260 cm`의 정지 Holding을 검사한다.
- 실제 Nav 경로를 Capsule 반지름 합과 여유 `10 cm` 이내에서 막는 가장 가까운 Holding 한 명만 양보한다.
- Holding은 예약을 유지한 채 로컬 경로 좌우 `130 cm`의 검증된 Nav 위치로 이동한다.
- 양보 위치가 없으면 강제 이동하지 않으며, 통과 또는 `1.5초` 제한 시간이 지나면 원래 Slot으로 복귀한다.
- 실패한 양보 탐색에도 `1초` 쿨다운을 적용한다.

## 디버그

- Enemy 머리 위 문자열에 `Traffic:<Role>`을 표시한다.
- `WaitIngressDeferred` Enemy에서 우선 통행 중인 Attack Enemy까지 주황색 선을 표시한다.
- `HoldingYield` Enemy에서 임시 양보 위치까지 보라색 선과 구를 표시한다.

## 검증

- Attack과 Wait가 같은 좁은 통로를 향할 때 Attack은 계속 이동하고 Wait만 `WaitIngressDeferred`가 되는지 확인한다.
- Attack이 Slot `55 cm` 안에 도착하면 보류된 Wait가 최대 약 `0.2초` 뒤 출발하는지 확인한다.
- 이미 이동 중인 Wait가 새로운 Attack 배정 때문에 병목 중앙에서 갑자기 정지하지 않는지 확인한다.
- 서로 다른 층의 겹쳐 보이는 경로가 Wait를 보류하지 않는지 확인한다.
- 이동이 정상 진행 중일 때 Stationary Holding이 양보하지 않는지 확인한다.
- 교착 경로 바로 앞의 Holding 한 명만 예약을 유지하며 양보하는지 확인한다.
- 이동자가 통과하거나 제한 시간이 지나면 Holding이 원래 Slot으로 복귀하는지 확인한다.
- 유효한 양보 위치가 없을 때 순간이동하거나 예약을 잃지 않는지 확인한다.

위 항목은 PIE 런타임 검증이 필요한 체크리스트이며 이번 작업에서는 실행하지 않았다. `Traffic:HoldingYield`와 보라색 표시는 코드의 자기 보고이므로 단독 합격 근거로 사용하지 않고, 실제 Actor 궤적·Holding 예약 배열·복귀 목적지를 함께 확인한다.

## 빌드

- `ProjectBHEditor Win64 Development -NoLink` C++ 컴파일 성공.

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.h`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`
