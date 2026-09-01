# Greystone AnimBP Preview Mesh 문제 해결

## 증상

기존 `ABP_Hero`를 열었을 때 Preview Mesh 목록에 Greystone Skeletal Mesh가 나타나지 않는다.

## 원인

Animation Blueprint는 생성될 때 특정 Skeleton에 묶인다. Preview Mesh 목록에는 기본적으로 그 Skeleton을 사용하는 Skeletal Mesh 또는 호환 Skeleton으로 등록된 Mesh만 나타난다.

따라서 기존 Hero용 `ABP_Hero`의 Skeleton과 Greystone의 Skeleton이 다르면, Greystone을 Preview Mesh로 고르거나 런타임에 그대로 적용할 수 없다. 이는 Preview Scene의 표시 오류가 아니라 Skeleton 호환성 제약이다.

## 먼저 확인할 것

1. `ABP_Hero`를 열고 Class Settings 또는 Asset Details에서 Target Skeleton을 확인한다.
2. Greystone Skeletal Mesh를 열고 Details의 Skeleton 경로를 확인한다.
3. 두 경로가 정확히 같은지 비교한다.

경로가 다르면 아래 해결 방식 중 하나를 선택한다.

## 권장 해결: Greystone 전용 Animation Blueprint

첫 Greystone 수직 슬라이스에는 기존 `ABP_Hero`의 Preview Mesh를 억지로 바꾸지 않는다.

1. Greystone Skeleton을 연다.
2. Content Browser에서 우클릭해 `Animation → Animation Blueprint`를 만들고, Target Skeleton으로 Greystone Skeleton을 선택한다.
3. 이름을 `ABP_Greystone`으로 두고 `Content/ProjectBH/Characters/Greystone/Animation/`에 만든다.
4. Greystone 원본 AnimBP가 있다면 이를 참고하거나, 필요한 이동 상태와 전투 Slot만 ProjectBH 전용 ABP에 구성한다.
5. Greystone 기반 플레이어 BP의 Mesh > Anim Class를 `ABP_Greystone`으로 지정한다.

## 콤보 Montage를 재생하기 위한 최소 조건

`ABHHeroCharacter`는 `PlayAnimMontage`로 `AM_Knight`를 재생한다. 따라서 `ABP_Greystone`의 Anim Graph 최종 출력 경로에는 Montage가 재생될 Slot 노드가 있어야 한다.

1. Locomotion State Machine 또는 기본 Pose를 만든다.
2. 그 출력에 `Slot` 노드를 연결한다.
3. `AM_Knight`와 AnimGraph 모두 `CombatGroup.UpperBody`를 사용한다.
4. [[상하체 애니메이션 분리 및 Enemy Run 연결 절차]]의 `Layered Blend Per Bone → FullBody Slot` 구조를 거쳐 Final Animation Pose에 연결한다.

Slot이 없으면 이동 애니메이션은 보여도 Montage 공격이 보이지 않을 수 있다.

## 최소 Locomotion Blend Space 연결

`Locomotion_BS`가 2D Blend Space라면 Anim Graph에서 다음처럼 연결한다.

```text
Ground Speed 변수 ──> Locomotion_BS의 Ground Speed 축
Direction 변수    ──> Locomotion_BS의 Direction 축
Locomotion Cached Pose + Slot(UpperBody) → Layered Blend Per Bone → Slot(FullBody) → Output Animation Pose
```

Blend Space 입력 핀에 `None`이라고 표시되는 것은 축 이름이 비어 있다는 뜻이다. `Locomotion_BS`를 열어 Asset Details의 Axis Settings에서 해당 축 이름을 `Ground Speed`로 바꾼다. 저장 후 Anim Graph로 돌아오면 `None` 핀이 `Ground Speed`로 바뀐다.

초기 확인 단계에서는 `Direction`을 `0.0`으로 두고, `Ground Speed`만 연결해 전진·정지 이동을 먼저 확인해도 된다. 좌우·후진 이동을 지원할 때는 공용 C++ AnimInstance가 계산한 상속 `Direction` 변수를 연결한다.

`Locomotion_BS`의 포즈 출력은 바로 Output Animation Pose에 연결할 수 있지만, 이동 공격을 재생하려면 Cached Pose와 `CombatGroup.UpperBody` Slot을 `Layered Blend Per Bone`으로 합성하고 마지막에 `CombatGroup.FullBody` Slot을 둔다.

## 좌우 게걸음(Strafe) 연결

`Direction` 변수만 계산해도 캐릭터가 이동 방향을 향해 자동 회전하면 값이 계속 0에 가까워져 게걸음이 보이지 않는다. Greystone은 캐릭터가 카메라/컨트롤러가 보는 방향을 유지한 채 이동하도록 설정한다.

### Codex C++ 설정

`ABHHeroCharacter`는 다음 이동 회전 설정을 사용한다.

- `bOrientRotationToMovement = false`
- `bUseControllerDesiredRotation = true`

따라서 좌우 이동 중에도 캐릭터는 컨트롤러 방향을 향하고, Velocity와 Actor Rotation의 차이로 Direction이 약 `-90` 또는 `90`이 된다.

### Codex: 공용 AnimInstance 계산

`GroundSpeed`와 `Direction`은 Blueprint Event Graph에서 새로 계산하지 않는다. 공용 `UBHCharacterAnimInstance`가 매 프레임 다음 값을 계산해 모든 Hero AnimBP에 `BlueprintReadOnly`로 제공한다.

- `GroundSpeed`: Velocity의 XY 길이
- `Direction`: 캐릭터가 보는 방향을 기준으로 한 Velocity 방향. 전진 0, 좌우 약 ±90, 후진 약 ±180

`ABP_Greystone`는 My Blueprint의 부모 클래스 변수 목록에서 상속된 `GroundSpeed`, `Direction`을 가져와 Blend Space 핀에 **Get으로만** 연결한다. `Set Direction`, `Get Actor Rotation`, `Calculate Direction` Event Graph 노드는 만들 필요가 없다.

Blend Space Axis Settings는 다음을 권장한다.

| 축 | 범위 | 용도 |
| --- | --- | --- |
| Ground Speed | `0 ~ 400` | 정지·보행·달리기 속도 |
| Direction | `-180 ~ 180` | 전진 0, 좌우 약 ±90, 후진 약 ±180 |

Direction의 부호가 예상과 반대로 보이면 Blend Space의 좌우 샘플 위치를 서로 바꾸거나, `Direction * -1`을 연결해 맞춘다.

### BP Override 확인

Greystone 플레이어 BP의 Character Movement 설정이 C++ 기본값을 덮어쓰지 않는지 확인한다. `Orient Rotation to Movement`가 켜져 있으면 끄고, `Use Controller Desired Rotation`은 켠다.

## 대안

- **Compatible Skeleton 등록**: 본 계층·이름이 거의 같은 경우에만 Greystone Skeleton에 기존 Skeleton을 호환으로 등록해 일부 Animation Sequence를 공유한다. Animation Blueprint 전체가 자동으로 호환되는 것은 아니므로, Montage·손·방패 자세를 반드시 확인한다.
- **IK Retargeter**: 기존 Hero의 이동 애니메이션이나 AnimBP에서 쓸 Animation Sequence를 Greystone Skeleton용 복제본으로 변환한다. 이 경우에도 최종 AnimBP는 Greystone Skeleton을 대상으로 새로 만든다.
- **Greystone 원본 AnimBP 사용**: 원본 AnimBP의 이동 품질이 충분하면 ProjectBH 전용 자식/복제본을 만들어 Slot과 필요한 상태만 추가한다. 원본을 직접 수정하지 않는다.

## 피해야 할 방식

- `ABP_Hero`의 Target Skeleton을 강제로 Greystone으로 바꾸려 하지 않는다. Animation Blueprint의 Target Skeleton은 기존 에셋에서 교체하는 설정이 아니다.
- Skeleton이 다른 상태에서 `ABP_Hero`를 Greystone Mesh의 Anim Class에 그대로 지정하지 않는다.
- 원본 Greystone AnimBP나 원본 Skeleton에 ProjectBH용 Socket·Notify·상태 로직을 직접 넣지 않는다.

## 관련 문서

- [[Paragon 스켈레톤 그룹화 및 리타기팅 검증]]
- [[Greystone 검방패 전투 수직 슬라이스 작업 목록]]
- [[Greystone Montage Notify 배치 절차]]
