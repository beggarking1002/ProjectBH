# Paragon 애니메이션 파일명 및 활용 규칙

## `_MSA`

`_MSA`는 **Mesh Space Additive** 애니메이션이다.

- 기본형(예: `Attack_A`): 완성된 절대 포즈 애니메이션. 일반 공격 상태, Anim Montage, 단독 미리보기/리타게팅의 기준으로 사용한다.
- `_MSA`형(예: `Attack_A_MSA`): 기준 포즈에 더할 회전·이동의 차이값(델타) 애니메이션. Base Pose 위에 `Apply Mesh Space Additive` 또는 해당 Additive 블렌드 경로로 얹어 사용한다.

두 파일은 미리보기에서 비슷해 보여도 런타임 용도가 다르다. `_MSA`를 일반 Animation Sequence처럼 단독 재생하거나 기본형과 함께 이중으로 재생하면 자세가 망가질 수 있다.

## ProjectBH 초기 수직 슬라이스 규칙

1. 일반 근접 공격과 회피에는 우선 **기본형 Animation Sequence**를 사용한다.
2. `_MSA`는 조준, 상체 보정, 이동 중 공격처럼 하체 로코모션 위에 상체 동작을 겹쳐야 하는 요구가 생겼을 때만 검토한다.
3. `_MSA`를 리타게팅해야 한다면 먼저 기본형을 정상 리타게팅해 기준 자세를 검증한다. 이후 Additive 설정과 Base Pose가 유지되도록 별도 검증한다.

## Montage Slot 선택

### 첫 검방 수직 슬라이스

`AM_Greystone_LightCombo`는 우선 `DefaultGroup.DefaultSlot`의 **전신 공격**으로 구성한다. 공격 중 이동 입력과 방향 전환의 세부 정책은 전투 상태에서 제어한다. 이 단계에서 억지로 이동 공격을 섞지 않는다.

### 이동 중 상체 공격이 필요한 단계

`UpperBody` Slot은 Montage에 넣는 것만으로 이동 공격이 되지 않는다. Greystone Animation Blueprint에서 다음 구조가 필요하다.

1. 로코모션 State Machine 출력을 Cached Pose로 저장한다.
2. Cached Pose를 `UpperBody` Slot의 Source Pose에 연결한다.
3. 원본 Cached Pose를 Base Pose, Slot 출력을 Blend Pose로 한 `Layered Blend Per Bone`을 구성한다.
4. Branch Filter 또는 Blend Mask를 `spine_01`부터 상체에만 적용한다.

이때 이동 애니메이션은 하체에 유지되고, Montage가 상체만 덮는다. 기본형 전신 공격은 하체 움직임과 어색할 수 있으므로, 이동 공격 품질이 필요해지는 시점에는 `_MSA` 또는 상체 전용 시퀀스를 우선 검토한다.

## Slot이 Montage에 없을 때

Slot은 Montage가 아니라 **Skeleton Asset에 저장**된다.

1. Greystone Skeleton을 연다.
2. `Anim Slot Manager`에서 `DefaultGroup` 아래 `DefaultSlot`이 있는지 확인한다. 없으면 `Add Slot (+)`으로 `DefaultSlot`을 만든 뒤 Skeleton을 저장한다.
3. 이동 상체 공격용은 같은 Group 아래 `UpperBody` Slot을 추가한다.
4. Montage로 돌아가 Slot Track의 드롭다운 또는 우클릭 메뉴에서 `New Slot`을 선택하고, 방금 만든 Slot을 지정한다.
5. 해당 Montage를 실제 재생하려면 Greystone AnimBP에도 같은 이름의 `Slot` 노드가 반드시 있어야 한다.
