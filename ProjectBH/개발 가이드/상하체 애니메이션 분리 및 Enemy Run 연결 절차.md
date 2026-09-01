# 상하체 애니메이션 분리 및 Enemy Run 연결 절차

> C++ 지원 상태: 완료. AnimBP와 Montage는 Unreal Editor에서 아래 절차로 연결한다.

## 1. 공통 Slot 정책

Player와 Enemy가 사용하는 각 Skeleton에서 `Anim Slot Manager`를 열고 다음 Slot을 만든다.

| Group | Slot | 용도 |
| --- | --- | --- |
| `CombatGroup` | `UpperBody` | 이동 가능한 가벼운 공격과 위협 동작 |
| `CombatGroup` | `FullBody` | 강공격, 피격, 사망, 구르기처럼 전신을 보존해야 하는 동작 |

두 Slot을 같은 Group에 두어 UpperBody와 FullBody Montage가 동시에 충돌하지 않게 한다. 기존 Montage Section과 Notify는 Slot Track을 바꿔도 유지된다.

## 2. 공통 AnimGraph 구조

`ABP_Knight`와 `ABP_Guardian`에 같은 구조를 만든다.

```text
Locomotion State Machine
        └─ Save Cached Pose: LocomotionPose

Use Cached Pose(LocomotionPose) ────────────────┐
                                                ├─ Layered Blend Per Bone
Use Cached Pose(LocomotionPose) → Slot(UpperBody)┘
                                                        ↓
                                                Slot(FullBody)
                                                        ↓
                                              Output Animation Pose
```

`Layered Blend Per Bone` 설정:

- Base Pose: 원본 `LocomotionPose`
- Blend Pose 0: `Slot(UpperBody)` 결과
- Branch Filter 시작 Bone: 우선 `spine_01`
- Blend Depth: 우선 `1`, 허리 경계가 딱딱하면 `2~3` 비교
- Mesh Space Rotation Blend: 켬

Skeleton마다 척추 Bone 이름이 다르면 Skeleton Tree에서 골반 바로 위 첫 척추 Bone을 사용한다. 다리까지 공격 포즈가 침범하면 시작 Bone과 Branch Filter를 먼저 확인한다.

## 3. Montage 분류

### Player

- `AM_Knight`의 이동 허용 기본 공격은 Slot Track을 `CombatGroup.UpperBody`로 바꾼다.
- UpperBody Montage와 원본 Animation Sequence의 Root Motion은 끈다.
- 발 스텝과 골반 회전이 타격감의 핵심인 공격은 별도 Montage로 분리해 `CombatGroup.FullBody`를 사용한다.

현재 3타 콤보는 우선 UpperBody로 연결해 이동 중 발이 계속 움직이는지 검증한다. 허리 회전이나 검 궤적이 크게 망가지면 해당 타격만 FullBody 공격으로 재분류한다.

### Enemy

- 현재 Guardian은 Attack Slot에 도착한 뒤 공격하므로 `AM_Guardian_Attack`은 우선 `CombatGroup.FullBody`를 사용한다.
- `AM_Guardian_HitReact`, `AM_Guardian_Death`도 `CombatGroup.FullBody`를 사용한다.
- 추후 이동 중 공격이나 대기 위협 동작을 추가할 때만 해당 Montage를 `CombatGroup.UpperBody`로 만든다.

## 4. Guardian 포메이션 합류 전 Run 연결

C++ `BHEnemyAnimInstance`가 다음 변수를 제공한다.

- `bIsChasing`: Combat State가 `Chasing`인지
- `bShouldMove`: 속도 히스테리시스를 통과한 안정적인 이동 여부
- `bHasJoinedFormation`: 현재 타깃의 예약 Slot에 한 번이라도 실제 도착했는지
- `bNeedsFormationCatchUp`: 합류 후 현재 Slot에서 크게 뒤처져 따라잡아야 하는지
- `bShouldUseRunLocomotion`: `Chasing && 이동 중 && (포메이션 미합류 || Catch-up 필요)`인지

`ABP_Guardian`의 기존 `Guardian_BS`와 상태 전환은 유지한다. Locomotion State 내부에서 결과 Pose만 선택한다.

1. `Locomotion` State를 열고 현재 `Guardian_BS` 노드는 그대로 둔다.
2. 우클릭하여 `Blend Poses by Bool` 노드를 만든다.
3. `False Pose`에 기존 `Guardian_BS` 결과를 연결한다.
4. `True Pose`에 반복 재생되는 전진 Run `Sequence Player`를 연결한다.
5. `Active Value`에는 `bShouldUseRunLocomotion`을 연결한다.
6. `Blend Poses by Bool` 결과를 State의 `Output Animation Pose`에 연결한다.
7. 노드 Details의 `False Blend Time`, `True Blend Time`은 우선 각각 `0.15~0.2초`로 둔다.

이 구조에서는 포메이션 밖 직접 추격과 Attack Slot 진입/Catch-up 이동에서만 Run을 사용한다. Wait, Holding, Pending을 배정받은 Enemy는 거리와 무관하게 기존 `Guardian_BS`를 사용한다. Attack Slot Enemy가 목표 Slot에서 300cm 이상 뒤처지면 Catch-up Run으로 전환되고, 160cm 안까지 따라잡으면 기존 Locomotion으로 복귀한다.

Enemy의 `GroundSpeed`는 CharacterMovement가 요청한 Velocity가 아니라 프레임 간 실제 월드 위치 변화로 계산한다. Detour Crowd가 이동 속도를 출력하면서도 군중에 막혀 제자리에 있는 경우 `ObservedGroundSpeed`가 0으로 내려가므로 Idle로 복귀한다.

Catch-up 기본값은 `BHCrowdEnemyAIController` 계열 Blueprint의 Class Defaults에서 조정한다.

| Category | 변수 | 기본값 | 의미 |
| --- | --- | ---: | --- |
| AI → Movement Intent | `Formation Catch Up Move Speed` | 500 cm/s | 접근·Catch-up 상태의 실제 이동속도 |
| AI → Engagement Slots | `Formation Catch Up Enter Distance` | 300 cm | 합류 후 Run을 시작하는 Slot 거리 |
| AI → Engagement Slots | `Formation Catch Up Exit Distance` | 160 cm | 기존 Locomotion으로 복귀하는 Slot 거리 |
| AI → Recovery | `Run No Progress Timeout` | 1.5 s | Attack Run이 목표에 접근하지 못할 때 교착 복구까지 허용하는 시간 |

Enter와 Exit 사이를 히스테리시스 구간으로 사용하므로 경계에서 Run과 기존 Locomotion이 반복 전환되지 않는다.

합류 상태가 초기화되는 경우:

- 타깃 변경
- 타깃 상실
- 전투권 완전 이탈
- 사망·AI 해제·Object Pool 재활성화

합류 상태가 유지되는 경우:

- Wait/Holding/Attack Slot 재배정
- 포메이션 Reform
- 피격과 일시적인 경로 실패

전진 Run 하나만 연결하면 접근 중 옆으로 크게 움직일 때 발 방향이 어색할 수 있다. 우선 전진 Run으로 의도를 검증하고, 필요할 때 전용 `ApproachRun_BS`에 좌우 Run을 추가한다. 기존 `Guardian_BS`의 `GroundSpeed`와 `Direction` 연결은 변경하지 않는다.

## 5. PIE 검증

1. Player가 이동하면서 3타 공격을 해도 하체 Locomotion이 계속 재생된다.
2. Player가 정지 상태에서 공격해도 다리가 과도하게 걷지 않는다.
3. Guardian이 최초 포메이션 Slot에 도착하기 전에는 Run을 사용한다.
4. 최초 Slot 도착 후 가까운 Wait→Attack 이동과 Reform에서는 기존 `Guardian_BS`를 사용한다.
5. Attack Slot Enemy는 Player가 도망가 Slot 거리 300cm 이상으로 벌어지면 Catch-up Run으로 바뀐다.
6. Slot 거리 160cm 안으로 복귀하면 기존 `Guardian_BS`로 돌아온다.
7. Wait/Holding/Pending Enemy는 Slot이 멀어져도 Run을 사용하지 않는다.
8. 이동 Velocity가 남아 있어도 실제 위치가 변하지 않으면 Run/Jog에서 Idle로 내려온다.
9. 전투권 밖으로 완전히 이탈했다가 다시 접근하면 Run이 다시 활성화된다.
10. Guardian 공격, 피격, 사망은 FullBody로 재생되어 하체 Locomotion이 섞이지 않는다.
11. UpperBody 공격 중 검 궤적과 `ANS Melee Hit Window`의 판정 시점이 기존과 크게 달라지지 않는다.

## 6. 흔한 문제

| 증상 | 확인할 것 |
| --- | --- |
| Montage가 전혀 보이지 않음 | Montage Slot Track과 AnimGraph Slot 이름이 동일한지 |
| 공격 중 다리도 멈춤 | Montage가 `FullBody` 또는 기존 `DefaultSlot`을 사용하고 있지 않은지 |
| 공격이 상체에 안 보임 | Layered Blend의 Blend Pose에 `UpperBody` Slot 결과가 연결됐는지 |
| 허리가 꺾임 | Branch Filter Bone, Blend Depth, Mesh Space Rotation Blend |
| 공격 중 Actor가 끌려감 | UpperBody Montage/Sequence의 Root Motion이 꺼져 있는지 |
| 최초 접근인데 Guardian이 여전히 걸음 | `Blend Poses by Bool`의 True Pose와 `bShouldUseRunLocomotion` 연결 확인 |
| Slot 사이에서도 계속 Run | ABP가 `bIsChasing`이 아니라 `bShouldUseRunLocomotion`을 사용하는지 확인 |
| Catch-up Run이 너무 자주 발생 | Enter Distance를 높이거나 Exit Distance를 낮춰 히스테리시스 폭 확대 |
| Run 발이 미끄러짐 | Formation Catch Up Move Speed와 Run Sequence의 원래 이동감 비교 |
