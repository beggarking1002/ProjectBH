# Corridor 측면·NavMesh 공백·Pocket 진입 문제 진단

> 2026-08-30 상태: Pocket Attack 동적 수용량과 Corridor 중앙부 Side별 수용량을 유지한다. Corridor 변은 복잡한 Spillover 대신 Pocket으로 전환하도록 구현했다. 폭 축 Attack 후보와 Cross-Channel 승격 실험은 롤백했다.

## 확인한 현상

1. Open 대형은 현재 비교적 안정적이다.
2. Corridor 중앙이 아닌 끝·구석에 Player가 서면 막힌 쪽의 빈 Attack Slot을 열린 쪽 Enemy가 채우지 않는다.
3. NavMeshBoundsVolume을 확장해도 특정 삼각형 영역에 NavMesh가 생성되지 않는다.
4. Pocket에서 Attack Slot이 남거나, 예약된 Enemy가 Attack Slot로 진입하지 않는 상황이 간헐적으로 발생한다.

## 1. Corridor 측면 문제

현재 Corridor는 다음 규칙을 사용한다.

- Attack Slot Index를 짝수·홀수로 나눠 양쪽 Side에 배분한다.
- Corridor 진입 시 Enemy의 `CorridorSideIndex`를 정하고 Corridor가 유지되는 동안 고정한다.
- Attack 승격은 같은 Channel을 우선하고, 예외도 같은 Side의 다른 Lane까지만 허용한다.
- 반대 Side Enemy가 Player를 가로질러 빈자리를 채우는 것은 금지한다.

이 규칙은 Player가 통로 중앙에 있을 때는 안정적이지만, 한쪽 끝이나 벽 가까이에 서면 막힌 Side의 수용량을 줄이지 못한다. 따라서 열린 Side에 충분한 공간과 Enemy가 있어도 막힌 Side의 빈자리가 유지된다.

### 권장 수정

- Corridor 양쪽의 실제 Navigation 여유 길이를 별도로 계산한다.
- 각 Side의 Attack Slot을 생성한 뒤 `목표점 투영 성공`, `투영 오차`, `Player에서 목표점까지 Nav 경로`로 유효성을 검사한다.
- Corridor 중앙에서는 기존 Side별 수용량과 Queue 규칙을 유지한다.
- Player가 폭 축의 한쪽 변에 치우치면 해당 공간을 Pocket으로 전환한다.
- 모든 반대 탐침 쌍에서 가장 비대칭인 경계를 찾는다. 가까운 거리 `100 cm 이하`, 반대쪽 여유 차이 `100 cm 이상`이면 Pocket으로 전환한다.
- 해당 Pocket 유지 중에는 `130 cm 이하 / 여유 차이 60 cm 이상`의 완화 기준을 사용한다.

## 2. NavMesh 삼각형 공백

`NavMeshBoundsVolume`은 NavMesh를 생성할 공간의 범위만 정한다. 범위 안의 Collision Geometry가 걷기 불가능하다고 판정되면 Volume을 키워도 공백은 채워지지 않는다.

우선 확인 순서:

1. `P`로 NavMesh를 표시한다.
2. `Alt + C` 또는 Collision 표시로 해당 지점의 실제 충돌을 확인한다.
3. `RecastNavMesh-Default`의 `Draw Poly Edges`, `Draw Tile Bounds`를 켠다.
4. 삼각형 위·아래의 Static Mesh, Blocking Volume, Nav Modifier를 선택한다.
5. 장식 Mesh가 바닥을 잘못 막는다면 `Can Ever Affect Navigation`을 끈다.
6. 바닥 Collision이 비어 있다면 바닥 Mesh의 Simple Collision을 고치거나 얇은 Blocking Volume으로 연속된 보행면을 만든다.
7. `Build → Build Paths` 또는 Navigation Build 후 다시 확인한다.

현재 `RecastNavMesh`의 Agent Radius는 `34 cm`, 캐릭터 Capsule 반지름은 약 `42 cm`, Corridor 계산 반지름은 `45 cm`다. NavMesh를 채우기 위해 Agent Radius를 더 줄이는 것은 실제 Capsule이 지나갈 수 없는 경로를 만들 수 있으므로 권장하지 않는다.

## 3. Pocket Attack Slot 문제

현재 Pocket 부채꼴은 한 Row에 Slot이 모두 들어가지 않으면 다음 Row의 반경을 `PocketRowSpacing = 100 cm`만큼 늘린다. 이 로직은 Attack에도 공통 적용된다.

- Attack Ring: `125 cm`
- 두 번째 Pocket Attack Row: `225 cm`
- Guardian Attack Start Range: `150 cm`
- Slot Acceptance Radius: `20 cm`
- 안전한 Attack Slot 중심 최대 거리: `130 cm`

따라서 두 번째 Row로 밀린 Attack Slot은 공격 거리 규칙을 만족할 수 없다. Pocket은 현재 `GetActiveAttackSlotCount()`에서도 항상 전체 5개를 활성으로 보고하므로, 공간상 들어갈 수 없는 5번째 Slot이 생길 수 있다.

### 권장 수정

- Pocket Attack Slot에는 다중 Row를 사용하지 않는다.
- 현재 Open Arc와 Capsule 간격 안에 실제로 들어가는 수만 `ActivePocketAttackSlotCount`로 계산한다.
- 유효 Attack 수가 3~4개라면 디버그도 `A:3/3`, `A:4/4`로 표시하고 나머지는 비활성 처리한다.
- Wait·Holding·Pending에는 기존 다중 Row 부채꼴을 유지한다.
- Attack Slot 후보는 Nav 투영 성공만 보지 말고 투영 전후 오차와 완전 경로까지 검증한다.
- Pocket에서 Wait → Attack 진입은 같은 각도의 외곽 Ingress Waypoint를 거친 뒤 최종 Slot로 이동하게 한다.

## 수정 우선순위

1. Pocket Attack 동적 수용량과 Attack 다중 Row 금지 — **구현 완료**
2. Corridor 중앙부 Side별 유효 수용량 계산 — **구현 완료**
3. Corridor 변의 Pocket 전환 — **구현 완료**
4. NavMesh 삼각형 공백의 Collision 원인 확인 및 레벨 수정
5. 선택 Enemy Crowd Debug로 Pocket 진입 실패가 예약·경로·군중 교착 중 어느 단계인지 검증

## 관련 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`
- `Config/DefaultEngine.ini`
- [[몬스터 이동 시스템 규칙]]
