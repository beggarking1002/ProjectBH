# Enemy 이동 버벅임과 Formation Anchor 진단

> 상태: EnemyPawn Collision 실험 롤백, Anchor 중앙 복귀, 버벅임 1차 완화의 코드 구현·빌드 완료. PIE 검증 대기. 현재 이동 규칙의 정식 명세는 [[몬스터 이동 시스템 규칙]]을 기준으로 한다.

## 요청

- Enemy 간 물리 충돌을 끄는 실험이 이동 버벅임을 해결하지 못했다.
- 버벅임이 AI 이동인지 Animation Blueprint인지 구분해야 한다.
- 플레이어가 멈춘 뒤에도 Wait/Holding Formation이 한쪽으로 치우쳐 남는 문제를 해결해야 한다.

## 현재 코드에서 확인한 사실

### 이동 목표 갱신

관련 코드: `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`, `Source/ProjectBH/AI/BHCrowdEnemyAIController.h`

- AI Controller는 Enemy별로 약 0.35~0.65초 간격으로 `RefreshTargetAndMove()`를 실행한다.
- 추격 목표가 100cm 이상 이동했을 때 `MoveToLocation()`을 다시 요청한다.
- Slot 이동 중에는 Attack 80cm, Wait 150cm, Holding/Pending 250cm 이상 목표가 움직이거나 경로 상태가 변하면 `MoveToLocation()`을 다시 요청한다.
- Enemy가 현재 Slot의 15cm 안에 들어오면 `StopMovement()`를 호출한다. Slot이 다시 멀어지면 다음 AI 갱신에서 새 이동을 요청하므로, 움직이는 Formation을 따르는 Enemy가 걸음-정지를 반복할 수 있다.

### Animation Blueprint에 전달되는 값

관련 코드: `Source/ProjectBH/AnimInstance/BHCharacterAnimInstance.cpp`, `Source/ProjectBH/AnimInstance/BHEnemyAnimInstance.cpp`

- `GroundSpeed`는 Character의 현재 2D Velocity를 필터링 없이 그대로 사용한다.
- `bHasAcceleration`도 CharacterMovement의 현재 Acceleration이 0보다 큰지를 그대로 사용한다.
- Detour Crowd의 조향과 정지 조정 중 Acceleration이 빠르게 바뀌면, Capsule은 움직이는데 Idle/Jog 상태만 흔들릴 수 있다.

### Formation Anchor

관련 코드: `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`, `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`

- Attack Slot은 현재 플레이어 위치를 중심으로 삼는다.
- Wait/Holding/Pending Slot은 `EngagementAnchorLocation`을 중심으로 삼는다.
- 이동 중 Anchor는 플레이어와의 거리가 `EngagementAnchorDeadZone` 175cm보다 클 때만 최대 350cm/s로 따라간다.
- 플레이어가 15cm/s 이하로 0.35초 유지하면 Anchor는 중앙 복귀를 시작한다. 200cm/s로 플레이어를 향해 이동하고 7.5cm 안에서 정확히 일치한다.
- 중앙 복귀 중 플레이어 속도가 40cm/s 이상이 되면 기존 지연 추종으로 복귀한다.

## 진단 결론

### Enemy 전용 Collision 실험

이동 버벅임을 해결하지 못했고 지형 관통 회귀 문제까지 만들었으므로, 전용 `EnemyPawn` Channel/Profile 실험만 롤백했다. Object Pool, 공격적 추격, 지연 Formation은 유지했다.

롤백한 대상은 `Config/DefaultEngine.ini`의 `EnemyPawn` Channel/Profile, `Source/ProjectBH/Collision/BHCollisionChannels.h`, `ABHEnemy::ApplyEnemyCapsuleCollisionProfile()`, Hero Capsule의 EnemyPawn 응답과 공격 Sweep의 EnemyPawn Query 추가분이다. 해당 파일들에 있던 다른 사용자 변경은 보존했다.

### 버벅임

코드만으로는 AI 이동과 Animation Blueprint 둘 다 유력하다. 현재 특히 의심되는 것은 다음 두 가지다.

1. 움직이는 Slot에 대한 이산적 `MoveToLocation()` 갱신과 Slot 도착 시 `StopMovement()`가 실제 Capsule을 걸음-정지로 만들 수 있다.
2. Animation Blueprint의 Idle/Jog 조건이 원시 `bHasAcceleration`에 의존하면 Crowd 조향에 따라 상태가 빠르게 바뀌 수 있다.

영상에서 Capsule까지 끊기면 AI/경로 문제로, Capsule은 매끄러운데 Mesh만 끊기면 Animation Blueprint 문제로 분류한다. 둘 다 끊기면 이동 명령과 Animation 조건이 같이 증상을 키우는 경우다.

## 구현 결과와 후속 진단

### 1. 정지 후 Anchor 중앙 복귀

속도는 플레이어의 2D Velocity를 기준으로 한다. 아래 수치는 구현된 초기값이며 PIE 검증 후 조정할 수 있다.

- 플레이어 이동 중: 현재 Dead Zone 방식으로 지연을 유지한다.
- 플레이어 속도가 15cm/s 이하로 0.35초 유지: `Settling` 상태로 전환한다.
- Settling: Anchor를 플레이어에게 200cm/s로 부드럽게 복귀시킨다.
- Anchor 오차가 7.5cm 이하면 플레이어 위치와 일치시킨다.
- 플레이어 속도가 40cm/s 이상으로 다시 올라가면 즉시 지연 추종으로 복귀한다. 정지/이동 임계값을 다르게 두어 입력 미세 진동을 방지한다.
- 속도가 15~40cm/s 사이면 상태 플래그를 바꾸지 않는다. 이미 Settling 중이면 중앙 복귀를 계속하고, 지연 추종 중이면 기존 Dead Zone 규칙을 계속 사용한다.
- 이 규칙은 Wait/Holding/Pending에만 영향을 주고 Attack Slot의 현재 플레이어 중심 규칙은 유지한다.

**코드 반영**

- `UCombatEngagementSlotComponent::UpdateEngagementAnchor()`에 정지 시간 누적과 중앙 복귀 상태를 추가했다.
- 제안한 초기값 `15 / 40 cm/s`, `0.35초`, `200 cm/s`, `7.5 cm`를 적용했다.
- 디버그 Anchor는 지연 추종 중 청록색, 중앙 복귀 중 노란색으로 표시한다.

### 2. 버벅임 1차 완화

- Path Following이 Velocity를 즉시 덮어쓰지 않고 `CharacterMovement`의 가속·감속을 사용하도록 설정했다.
- Slot은 정확한 점이 아니라 반경 `20 cm`의 허용 구역으로 처리한다. 속도가 `5 cm/s` 이하가 된 뒤 최종 정지·공격을 수행한다.
- Wait·Holding·Pending은 허용 구역 안에서 감속하는 동안 진행 방향을 유지하고, 정지 뒤에만 Player Focus로 전환한다.
- Slot 반경 안에서 아직 감속 중이면 같은 지점으로 0거리 Move 요청을 다시 만들지 않는다.
- Enemy AnimInstance에 `bShouldMove`를 추가했다. `10 cm/s` 이상에서 이동 상태로 들어가고, `5 cm/s` 이하에서 정지 상태로 나오는 히스테리시스를 사용한다.
- 현재 Enemy ABP의 기존 `bHasAcceleration` 전환을 수정하지 않아도 바로 시험할 수 있도록, Enemy에서만 `bHasAcceleration = bShouldMove` 호환 연결을 제공한다.

이는 두 유력 원인을 기반으로 한 저위험 1차 완화이며 원인 확정은 아니다. PIE에서 Capsule과 Mesh를 따로 관찰해 남은 원인을 분류한다. 세부 구현 기록은 [[2026-08-29 - Enemy 이동 버벅임 1차 완화 구현]]에 둔다.

## 빌드 검증

- UE 5.7 UnrealHeaderTool 성공
- `CombatEngagementSlotComponent.cpp`, `BHCrowdEnemyAIController.cpp`, `BHEnemyAnimInstance.cpp`, `BHEnemyPoolManager.cpp` 컴파일 성공
- `UnrealEditor-ProjectBH.dll` 링크 성공
- `ProjectBHEditor Win64 Development` 결과: `Succeeded`
- 남은 경고는 기존 Visual Studio 14.38 Toolchain이 비권장 버전이라는 경고뿐이다.

## PIE 검증 대기 항목

- 플레이어가 15cm/s 이하로 0.35초 유지한 뒤에만 Anchor가 청록색에서 노란색으로 바뀐다.
- 노란 Anchor가 플레이어 중심으로 이동하고, Wait·Holding·Pending Slot 중심도 함께 복귀한다.
- 중앙 복귀 중 15~40cm/s로 움직이면 복귀를 계속하고, 40cm/s 이상으로 움직이면 즉시 청록색 지연 추종으로 전환한다.
- Anchor가 플레이어와 7.5cm 이내에 들면 중심 위치가 일치한다.
- Attack Slot은 변경 전처럼 플레이어 현재 위치를 중심으로 사용한다.
- Collision 롤백 후 Enemy가 바닥과 벽을 관통하지 않고, Player와의 충돌과 Player 공격 Sweep이 기존처럼 동작한다.
- 외곽 Ring 대칭은 디버그 Slot 구체의 중심이 플레이어 중심과 일치하는지로 우선 판정한다. 점유 Enemy 분포가 치우치면 Slot 예약 분포를 별도로 진단한다.
- 디버그 색과 Slot 구체는 내부 상태를 그대로 표시하므로 단독 합격 근거로 사용하지 않는다. 평평한 맵에서 Wait 8자리를 채우고 플레이어가 3초 이상 정지한 뒤, 실제 Wait Enemy Actor 위치의 2D 중심과 Player Actor 위치가 첫 허용값 50cm 안에 드는지 독립적으로 대조한다.
- Slot/Crowd 디버그는 현재 C++ 기본값으로 켜져 있다. 표시가 없으면 Hero의 `CombatEngagementSlots` Component에서 `Draw Debug Slots`, Enemy AI Controller Class Defaults에서 `Draw Crowd Debug`를 확인한다.
- `show collision` 상태에서 Slot 진입 시 Capsule이 자연 감속하는지, `20 cm` 허용 구역에서 정확한 중심점을 향해 앞뒤로 보정하지 않는지 확인한다.
- Enemy가 감속 중일 때 Mesh가 Idle로 먼저 떨어지지 않고, 완전 정지 부근에서 한 번만 Idle로 전환하는지 확인한다.

## 동영상 진단 촬영 조건

- 10~20초, 가능하면 60fps로 촬영한다.
- 플레이어 정지 -> 옆으로 이동 -> 다시 정지 순서를 한 클립에 담는다.
- Enemy 전체가 보이는 장면과 특정 Enemy 하나의 발/몸통이 보이는 장면을 포함한다.
- 콘솔에서 `show collision`을 실행해 Capsule이 같이 끊기는지 확인한다.
- Slot/Crowd 디버그 표시를 켜서 어떤 역할과 경로 단계에서 증상이 나타나는지 보이게 한다.

영상만으로 분류가 애매하면 다음 계측을 추가한다: Capsule 위치·Velocity·Acceleration, Move 요청/완료/중단 시각, `StopMovement()` 호출, 목표 Slot과 Anchor 위치, 현재 Anim State.

## 다음 작업

1. PIE에서 1차 완화 전후의 Capsule과 Mesh를 비교한다.
2. Capsule만 남아 끊기면 Move 요청/완료/중단 시각을 계측한다.
3. Mesh만 남아 끊기면 Anim State와 Blend Space 설정을 확인한다.
4. PIE 검증 대기 항목을 순서대로 확인한다.
