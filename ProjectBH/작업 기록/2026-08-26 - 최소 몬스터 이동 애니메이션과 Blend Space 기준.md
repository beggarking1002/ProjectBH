# 2026-08-26 - 최소 몬스터 이동 애니메이션과 Blend Space 기준

## 확인된 에셋 상태

- 전방 Walk는 제자리(In-place)로 보인다.
- 후방·좌·우 Walk는 Animation Preview에서 Mesh가 원점 밖으로 이동한다. Root Bone 이동이 포함된 Root Motion 계열일 가능성이 높다.

## 결정

서로 다른 Root Motion 기준의 애니메이션을 하나의 Blend Space에 즉시 섞지 않는다.

- CharacterMovement/NavMesh가 이동을 담당하는 네트워크 근접 적에는 모든 locomotion 애니메이션이 In-place여야 한다.
- Root Motion 애니메이션과 In-place 애니메이션을 혼합하면 Blend 중 메시 미끄러짐, Capsule과 Mesh의 불일치, 서버 이동 보정 문제가 발생한다.

## 최소 몬스터 단계

- 초기 근접 몬스터는 추적 이동 시 이동 방향을 향하도록 설정하고, 전방 In-place Walk 하나만 사용한다.
- 따라서 현재 단계에서 Direction Blend Space는 필수가 아니다.

### In-place 통일 후 Blend Space 구성

- 모든 Walk가 In-place로 확인됐으므로, 방향 이동/스트레이프 확장용 2D Blend Space를 이제 만들 수 있다.
- Guardian Skeleton 기준 Blend Space의 축은 다음으로 둔다.
  - Horizontal `Direction`: `-180 ~ 180`
  - Vertical `Ground Speed`: `0 ~ 몬스터 Max Walk Speed` (초기값은 `0 ~ 300` 권장)
- 샘플 위치:
  - Idle: `(Direction 0, Speed 0)`
  - Forward Walk: `(0, MaxSpeed)`
  - Right Walk: `(90, MaxSpeed)`
  - Left Walk: `(-90, MaxSpeed)`
  - Backward Walk: `(180, MaxSpeed)`; 경계 전환이 거칠면 동일 애니메이션을 `(-180, MaxSpeed)`에도 추가한다.
- `ABP_Guardian`의 부모는 `BH Enemy Anim Instance`로 둔다. 상속된 `GroundSpeed`, `Direction` Get 노드를 Blend Space 동명 핀에 연결한다. Event Graph에서 다시 계산하지 않는다.

단순 추적 AI가 이동 방향으로 몸을 돌리는 동안에는 Direction이 거의 0이어서 Forward Walk만 출력된다. 이는 정상이다. 좌·우·후방 샘플은 적이 플레이어를 바라본 채 원형 이동하거나 뒤로 물러나는 행동을 추가했을 때 사용된다.

### Blend Space가 한 번만 재생되고 멈출 때

- Animation Sequence 에셋의 Loop 설정이 아니라, AnimGraph에서 Blend Space를 재생하는 **`Blend Space Player` 노드**가 반복 여부를 결정한다.
- `ABP_Guardian`에서 Locomotion State를 열고 `Locomotion_BS` Player 노드를 선택한다.
- Details의 `Loop` 또는 `Loop Animation`을 켠다. Node에 Loop 핀이 노출돼 있다면 `true`인지 확인한다.
- Compile, Save 후 Preview와 PIE에서 확인한다.

그래도 한 번 재생 후 멈추면 State Machine Transition을 확인한다. Locomotion State에서 다른 State로 자동 이동하는 Transition이 있거나, State의 최종 출력이 Blend Space Player가 아닌 단발 Sequence Player로 연결돼 있으면 그 경로를 수정한다.

## 추후 스트레이프 확장

적이 플레이어를 바라본 채 좌·우·후방으로 움직이는 행동이 필요해질 때만 2D Blend Space를 만든다.

1. 후방·좌·우 시퀀스를 원본 보존 상태로 Duplicate한다.
2. 복제본에서 Root Motion을 제거/고정해 모두 In-place로 통일한다. Animation Preview에서 Root가 원점에 남는지 확인한다.
3. `Speed (0~이동 최대 속도)` × `Direction (-180~180)` 2D Blend Space에 Forward/Backward/Left/Right를 배치한다.
4. 각 시퀀스의 이동 거리, 재생 속도, 발 접지가 비슷한지 검증한다.

### UE 에디터에서 Root Motion 이동을 잠그는 절차

원본 애니메이션은 보존하고 후방·좌·우 시퀀스를 각각 Duplicate한 뒤 복제본에만 적용한다.

1. 복제한 Animation Sequence를 연다.
2. Viewport의 `Character > Bones > All Hierarchy`를 켠다. Root Bone에서 시작점과 현재 위치를 잇는 빨간 선이 보이면 Root Bone 이동 데이터가 있는 시퀀스다.
3. Asset Details의 `Root Motion`에서 아래처럼 설정한다.
   - `Enable Root Motion`: 끔
   - `Force Root Lock`: 켬
   - `Root Motion Root Lock`: 우선 `Anim First Frame`
4. Save한 뒤 Preview에서 Mesh가 시작점에서 이탈하지 않고 제자리 걸음을 하는지 확인한다.
5. 모든 locomotion 복제본에서 같은 기준으로 처리하고 Blend Space에 복제본만 사용한다.

`Force Root Lock`은 Root Motion을 추출해 Actor 이동에 적용하지 않더라도 Root Bone을 잠글 수 있다. 설정은 Root 이동 데이터를 삭제하는 것이 아니라 재생 때 고정하는 방식이다.

### Root Lock으로 해결되지 않는 경우

- Root Bone은 고정인데 `pelvis`/`hips` 등 하위 본에 전진 이동이 구워져 있으면 Root Motion 문제가 아니다. 위 설정만으로는 In-place가 되지 않는다.
- 이 경우 FBX 원본에서 root 또는 hips의 수평(X/Y) 이동 키를 제거해 다시 import하거나, DCC(Blender/Maya)에서 In-place 복제본을 만든다.
- 원본 FBX가 없다면 해당 방향 애니메이션은 이번 몬스터 이동에서 제외하고 전방 In-place Walk만 쓴다.

## 사망 시 뒤로 날아가는 애니메이션

사망 뒤로 밀려나는 변위가 연출의 일부라면 locomotion과 다르게 **Root Motion을 유지**한다.

1. 원본 Death Sequence를 Duplicate한다.
2. 복제 Death Sequence의 `Enable Root Motion`을 켠다.
3. `Root Motion Root Lock`은 우선 `Anim First Frame`으로 둔다. `Force Root Lock`은 켤 필요 없다.
4. Death Sequence를 Death Montage에 넣고, AnimBP Class Defaults의 `Root Motion Mode`를 `Root Motion from Montages Only`로 둔다.
5. 서버가 사망을 확정할 때 Death Montage를 재생하고, 일반 이동 AI/CharacterMovement는 정지시킨다.

이 설정은 사망 중 Root 이동 델타를 Character Capsule/Actor 이동에 적용하므로 Mesh만 뒤로 빠졌다가 되돌아오는 현상을 막는다. Root Motion이 없는 locomotion Blend Space에는 이 애니메이션을 넣지 않는다.

만약 Preview의 빨간 Root 이동선이 없으면 실제 Root Motion이 아니라 하위 본 연출일 수 있으므로 Enable Root Motion만으로 Actor가 이동하지 않는다. 그 경우는 연출을 제자리로 쓰거나 원본에서 Root 이동을 만든 뒤 재import한다.

## 주의

- `Enable Root Motion`만 켜서 문제를 해결하려 하지 않는다. 그것은 CharacterMovement 이동 대신 애니메이션이 Actor를 이동시키게 하는 선택이며, 현재 서버 권한 NavMesh 적의 기본 이동 방식과 다르다.
- Root Motion을 사용할 의도가 있다면 모든 locomotion 에셋과 네트워크 이동 설계를 별도로 Root Motion 기준으로 맞춰야 한다.
