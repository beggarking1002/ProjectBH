# 벽면 군중 Traversal 설계 초안

> 문서 역할: 미구현 벽면 Traversal의 설계 배경 기록이다. 지상 이동을 포함한 전체 규칙과 현재/미구현 상태 구분은 [[몬스터 이동 시스템 규칙]]을 단일 기준으로 사용한다.

## 목표

몬스터 떼가 성벽·절벽·건물 외벽의 지정된 침입 지점을 타고 올라와 플레이어 전투 구역으로 진입한다. 단일 적의 등반이 아니라, 여러 적이 서로 겹치지 않고 레인을 나누어 올라오는 장면을 만든다.

## 결론

일반 NavMesh를 벽 전체에 생성하지 않는다. 지상과 상단 NavMesh를 **Smart Nav Link 기반 Traversal Link**로 연결하고, Link 내부에서는 별도의 등반 레인 예약과 Wall Climb 이동 상태를 사용한다.

```text
지상 NavMesh
  → Traversal Entry 대기열
  → Smart Nav Link 도달
  → Climb Lane 예약
  → Wall Climb 상태
  → Traversal Exit
  → 상단 NavMesh
  → 전투 Slot Manager
```

Nav Link Proxy는 직접 연결되지 않은 두 NavMesh 영역을 경로상 연결할 수 있고, Smart Link에 도달한 AI의 Path Following을 일시 정지한 뒤 사용자 정의 동작을 실행하고 재개할 수 있다. 따라서 “등반 중”에는 보통 보행 경로 대신 별도 상태를 실행하는 용도로 적합하다.

## `ATraversalWallLink` 설계

`ANavLinkProxy`를 상속한 ProjectBH 전용 Actor로 만든다.

| 구성 요소 | 책임 |
| --- | --- |
| Entry Point | 지상 NavMesh에서 몬스터가 모이는 시작점 |
| Exit Point | 상단 NavMesh로 복귀하는 위치 |
| Smart Nav Link | 지상과 상단의 경로 연결 및 Traversal 시작 신호 |
| Climb Lanes | 벽 너비를 나눈 개별 등반 경로. 레인마다 한 몬스터만 점유 |
| Capacity/Queue | 동시에 등반 가능한 수와 대기열 관리 |
| Debug Draw | Entry, Exit, 레인 점유자, 대기열 표시 |

첫 버전은 4개 레인으로 시작한다. 레인 수는 벽 너비와 몬스터 Capsule 지름·안전 여유로 결정한다.

## 적 상태 전환

```text
MoveToTraversalEntry
  → WaitForClimbLane
  → ClimbStart
  → WallClimb
  → ClimbExit
  → ResumePathFollowing
```

- `MoveToTraversalEntry`: Detour Crowd가 지상에서 서로 비키며 Entry까지 이동한다.
- `WaitForClimbLane`: 빈 레인이 없으면 Entry 주변의 대기 슬롯을 점유한다.
- `ClimbStart`: Smart Link 도달 후 레인을 예약하고 Path Following을 멈춘다.
- `WallClimb`: 레인에 고정된 궤적과 등반 Montage를 사용한다. 서로 다른 레인은 독립적으로 진행한다.
- `ClimbExit`: Exit 위치에서 `MOVE_Walking`으로 복귀하고 레인을 반납한다.
- `ResumePathFollowing`: NavMesh 경로를 이어 상단 목적지 또는 전투 슬롯으로 이동한다.

## 이동 구현 원칙

`WallClimb`에는 `CharacterMovementComponent`의 `MOVE_Custom` 하위 모드를 사용한다. 일반 보행 NavMesh와 Detour Crowd는 지상/상단에서만 사용한다.

- 서버가 레인 예약·등반 시작·이동 상태·완료를 확정한다.
- 등반 중 위치는 레인 Curve/Spline을 따라 이동하고, Capsule Sweep으로 벽면 이탈과 큰 충돌만 검사한다.
- 등반 애니메이션은 레인 이동 시간과 동기화한다. 첫 버전에서는 루트 모션의 물리 이동에 전적으로 의존하지 않는다.
- 경직·사망·벽 파괴·Exit 막힘 시 즉시 레인을 반납하고, 추락/사망/대기 복귀 중 하나를 명시적으로 처리한다.

## 군중 연출 규칙

- 한 레인에는 동시 1마리만 허용한다.
- 뒤따르는 몬스터는 같은 레인의 일정 간격 큐가 아니라, 지상 Wait Slot에서 대기한다. 벽면 Capsule 겹침을 원천 방지한다.
- 카메라에 가까운 4~8마리만 실제 Character와 레인 이동을 사용한다.
- 먼 벽면의 대규모 몬스터는 간소화된 애니메이션/인스턴싱/스폰 연출로 대체한다. 이들은 핵심 전투 Collision과 Slot 점유에 참여하지 않는다.

## 포트폴리오 증명 포인트

1. 일반 `MoveTo`만 쓴 경우: 벽 앞에서 적이 뭉치고 상단에 도달하지 못함.
2. Traversal Link 적용: 지상 대기열 → 레인 분산 → 상단 전투 슬롯 합류.
3. 레인 수를 2/4/6으로 바꿨을 때 진입 시간, 대기 시간, 겹침 수를 비교.
4. 플레이어가 상단에서 기다릴 때와 성벽을 떠날 때의 적 재목표화 규칙을 제시.

## 단계별 구현

1. 단일 적이 Smart Link를 통해 지정된 벽을 등반하고 상단 NavMesh로 복귀한다.
2. 4개 레인과 예약/반납을 추가한다.
3. 8마리 접근 시 지상 대기열과 레인 분산을 검증한다.
4. 상단 도착 적을 기존 Combat Engagement Slot Manager에 연결한다.

## 참고

- [UE: Nav Link Proxy](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/ANavLinkProxy)
- [UE: NavMesh를 연결하는 Navigation Link Proxy](https://dev.epicgames.com/documentation/unreal-engine/overview-of-how-to-modify-the-navigation-mesh-in-unreal-engine)
- [UE: Character Movement의 Custom Movement Mode](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/GameFramework/UCharacterMovementComponent/CustomMovementMode)
