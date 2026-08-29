# 포위 범위 Player Focus 이동

> 상태: C++ 구현과 `ProjectBHEditor Win64 Development` 빌드 완료. PIE와 방향 애니메이션 검증 대기. 정식 규칙은 [[몬스터 이동 시스템 규칙]]을 기준으로 한다.

## 요구사항

- 포위 범위 밖에서 추격할 때는 이동 방향을 보고 달린다.
- Engagement Formation 상태에 있는 동안 역할과 경로 단계에 관계없이 항상 Player를 바라본다. `EngagementExitRadius` 밖으로 나가 Pursuit로 돌아가면 이동 방향 회전도 복구한다.
- Player 기준 옆으로 이동하면 게걸음, 뒤로 이동하면 후퇴 애니메이션을 사용한다.

## 구현

`ABHCrowdEnemyAIController`의 회전 정책을 두 구간으로 단순화했다.

### 포위 범위 밖 Pursuit

- `bOrientRotationToMovement = true`
- `bUseControllerDesiredRotation = false`
- Player Focus 해제
- 이동 방향을 보고 전력 추격한다.

### 포위 범위 안 Formation

- `bOrientRotationToMovement = false`
- `bUseControllerDesiredRotation = true`
- Player Focus 유지
- InitialCharge, Attack·Wait·Holding·Pending 이동, Ring 정렬, Ingress, Slot 감속과 정지에 동일하게 적용한다.

Wait 도착 시 회전 정책이 바뀌지 않으므로 `SettledFacingDelay`와 정착 회전 상태는 제거했다. Wait에서 발생하던 도착 정지와 회전 모드 변경의 중첩도 함께 사라진다.

## Animation Blueprint 계약

C++ AnimInstance의 `Direction`은 Actor가 Player를 바라보는 상태에서 월드 Velocity를 로컬 방향으로 변환한다.

- Player 쪽 전진: 약 `0°`
- Player를 바라본 채 오른쪽 이동: 약 `+90°`
- Player를 바라본 채 왼쪽 이동: 약 `-90°`
- Player를 바라본 채 후퇴: 약 `±180°`

코드는 이 값을 제공하지만 실제 게걸음 표현은 Enemy Locomotion Blend Space에 좌·우·후퇴 샘플이 있어야 한다. 샘플이 없거나 모두 전진 애니메이션이면 방향은 맞아도 발이 미끄러져 보일 수 있다.

## 빌드

- UnrealHeaderTool 성공
- `BHCrowdEnemyAIController.cpp` 컴파일 성공
- `UnrealEditor-ProjectBH.dll` 링크 성공
- 결과: `Succeeded`
- Visual Studio 14.38 비권장 Toolchain 경고 외 신규 오류 없음

## PIE 확인

1. 포위 범위 밖 Pursuit에서 `Facing:Move`인지 확인한다.
2. 포위 범위에 들어오는 순간 `Facing:Target`으로 바뀌는지 확인한다.
3. Wait/Holding Slot로 좌우 이동할 때 몸은 Player를 향하고 `Direction`이 약 `±90°`인지 확인한다.
4. Player 반대 방향으로 물러날 때 `Direction`이 약 `±180°`인지 확인한다.
5. Slot 도착 전후 `Facing:Target`이 유지되어 회전 정책 전환이 없는지 확인한다.
6. 좌·우·후퇴 Blend Space 샘플의 방향과 발 미끄러짐을 확인한다.
7. InitialCharge, Approach Ring, Align Ring, Attack Ingress, Wait, Holding, Pending 각각에서 `Facing:Target`인지 확인한다.
8. Formation 안에서 Player 쪽으로 전진할 때 `Direction`이 약 `0°`인지 확인한다.
9. Player가 움직여도 Enemy 몸통이 실제 Player를 계속 추적하는지 확인한다.
10. `EngagementExitRadius` 밖으로 멀어졌을 때 Pursuit와 `Facing:Move`로 복귀하는지 확인한다.

## 사용자 에디터 작업

Enemy Animation Blueprint 또는 Locomotion Blend Space에서 다음 샘플 배치를 확인한다.

- `Direction -90`: Left Strafe
- `Direction +90`: Right Strafe
- `Direction -180 / +180`: Backward
- `GroundSpeed 0`: Idle

좌우가 반대로 재생되면 현재 Skeleton·Mesh 전방축 기준으로 `-90/+90` 샘플을 서로 바꾼다.
