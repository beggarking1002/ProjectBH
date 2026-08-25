# Paragon 일체형 무기 제거 및 교체 절차

## 목표

Paragon 캐릭터의 원래 무기 시각 요소를 원본 에셋 수정 없이 감추고, ProjectBH의 별도 무기를 장착할 수 있는지 확인한다.

## 중요한 구분

`weapon_l`, `weapon_r`는 Skeleton의 **본**이다. 이 본은 지우지 않는다. 지울 대상은 캐릭터 Skeletal Mesh에 그려진 원래 무기 **형상**이다.

아래 순서대로 진행하며, 성공한 단계에서 멈춘다.

```text
원본 무기 형상
  ├─ 전용 무기 본에만 연결됨 → A. 본 숨김으로 해결
  ├─ 별도 머티리얼 슬롯을 씀 → B. BP 머티리얼 오버라이드로 해결
  └─ 몸 메시와 섞여 있음 → C. Blender 파생 Skeletal Mesh
```

## 사전 준비

- 원본 Paragon 폴더는 수정하지 않는다.
- 테스트용 `BP_PBH_<Hero>_VisualTest`를 ProjectBH 전용 폴더에 만든다. 원본 캐릭터 BP가 있다면 자식 BP로 만들고, 없다면 Character BP를 만들어 원본 Skeletal Mesh만 할당한다.
- BP의 Mesh가 원본 캐릭터 Skeletal Mesh를 쓰도록 한다.
- `weapon_r`를 오른손 무기 기준으로, `weapon_l`를 왼손 무기·방패 기준으로 사용한다.

## A. 본 숨김으로 원래 무기 감추기 — 가장 먼저 시도

### 사용자: Unreal Editor 작업

1. `BP_PBH_<Hero>_VisualTest`를 열고 Event Graph로 간다.
2. `Event BeginPlay`에서 Mesh 컴포넌트를 가져온다.
3. Mesh에서 **Hide Bone by Name** 노드를 만든다.
4. `Bone Name`에 `weapon_r`를 넣고, `Phys Body Op`은 `PBO_None`으로 둔다.
5. 테스트 맵에 BP를 배치하거나 Play로 실행한다.
6. Idle, 이동, 원래 공격 애니메이션을 재생해 원래 오른손 무기만 사라지는지 확인한다.
7. 왼손 무기도 있는 캐릭터면 `weapon_l`에 같은 시험을 추가한다.

### 성공 조건

- 원래 무기만 보이지 않는다.
- 손·팔·몸 메시가 사라지거나 찢어지지 않는다.
- 공격 중 원래 무기가 다시 나타나지 않는다.

성공하면 새 ProjectBH 무기 메시를 BP Components 패널에서 Mesh의 자식으로 추가하고, Details의 Parent Socket/Bone Name에 `weapon_r`를 지정한다. 위치·회전·스케일을 조절해 맞춘다. 무기 모델은 별도 Static Mesh 또는 Skeletal Mesh Component로 둔다.

> 주의: 테스트 중에는 `weapon_r` 본에 직접 장착한다. 최종 오프셋을 고정해야 할 때만 원본 Skeletal Mesh를 복제한 뒤, 복제본에 Mesh Socket을 만든다.

### 실패 시 판정

- 무기가 그대로 남는다: 무기 형상이 `weapon_r`만 따르지 않거나, 다른 본에 가중치가 섞여 있다.
- 손·팔도 함께 사라진다: 무기와 손/팔 정점이 같은 본 가중치를 쓴다.
- 공격 중 원래 무기와 새 무기가 겹친다: 원래 무기 형상을 충분히 감추지 못했다.

위 셋 중 하나면 A는 포기하고 B를 확인한다. 애니메이션이나 Skeleton 본을 삭제해서 해결하지 않는다.

### A 단계 트러블슈팅

`Event BeginPlay`는 블루프린트 편집기 뷰포트에서 캐릭터를 바라보는 것만으로는 호출되지 않는다. 레벨에 BP를 배치하거나 GameMode의 실제 Pawn으로 스폰한 뒤, 반드시 PIE의 Play 버튼으로 실행해서 확인한다.

1. `Hide Bone by Name` 다음에 `Print String`을 잠시 연결해 "Weapon hide called"가 화면에 나오는지 확인한다.
2. 출력이 없다면 테스트 BP가 실제로 스폰되지 않았거나, 현재 보는 캐릭터가 다른 BP/원본 Skeletal Mesh Actor다. 레벨에 테스트 BP를 직접 배치하고 Play한다.
3. 출력은 있는데 원래 무기가 그대로라면 `weapon_r`/`weapon_l` 본은 존재하지만 무기 형상이 그 본에만 스킨되지 않은 경우다. 이 시점에는 A를 실패로 기록하고 B(전용 머티리얼 슬롯)를 확인한다.
4. 공격 중에만 원래 무기가 보인다면, 공격 애니메이션이 원래 무기를 별도 컴포넌트로 부착하거나 다른 본을 쓸 수 있다. Components 목록과 Animation Blueprint의 Notify를 확인한다.

## B. 무기 전용 머티리얼 슬롯 숨기기

### 확인

1. 원본 Skeletal Mesh를 열고 Details의 Material Slots를 본다.
2. 무기만을 위한 슬롯(예: Weapon, Sword, Gun처럼 구분된 이름)이 있는지 찾는다.
3. 있다면 ProjectBH 폴더에 원본 머티리얼의 파생 Material Instance 또는 복제 머티리얼을 만든다.

### 적용

1. 파생 머티리얼을 무기 형상을 보이지 않게 만드는 방식으로 설정한다. 필요하면 Material Blend Mode를 Masked로 하고 Opacity Mask를 0으로 만든다.
2. **원본 Skeletal Mesh가 아니라 테스트 BP의 Mesh Component**에서 해당 Material Element만 파생 머티리얼로 Override 한다.
3. 무기가 사라지는지와 함께 그림자, 절단면, LOD, 공격 애니메이션을 확인한다.

### 사용 기준

무기만 전용 슬롯인 경우에만 쓴다. 갑옷·손·무기 등이 같은 머티리얼 슬롯이면 이 방법을 쓰지 않는다.

## C. Blender에서 ProjectBH 전용 파생 Skeletal Mesh 만들기

A/B가 실패했고, 그 캐릭터가 자유 무기 교체가 꼭 필요한 최종 캐릭터일 때만 한다.

### 사용자: Unreal Editor 작업

1. 원본 Skeletal Mesh를 선택하고 Asset Actions > Export로 FBX를 내보낸다.
2. 작업 사본임을 알 수 있는 이름으로 저장한다. 원본 FBX나 원본 Paragon 에셋을 덮어쓰지 않는다.

### 사용자: Blender 작업

1. FBX를 임포트하고 Armature와 Mesh가 모두 들어왔는지 확인한다.
2. Mesh의 Edit Mode에서 원래 무기 형상 정점만 선택한다.
3. 무기 형상을 삭제하거나, 별도 오브젝트로 분리한다. 최종 교체형 캐릭터라면 보통 본체에서 삭제한다.
4. Armature의 본 이름·계층, 캐릭터 본체의 스킨 가중치, UV, 머티리얼 슬롯은 바꾸지 않는다.
5. FBX로 내보낸다.

### 사용자: Unreal Editor 재임포트 작업

1. `Content/ProjectBH/Characters/Derived/<Hero>/`에 새 FBX를 임포트한다.
2. Import 옵션에서 원본 영웅의 기존 Skeleton을 지정한다. Skeleton을 새로 만들지 않는다.
3. 새 Skeletal Mesh만 만든다. 원본 Animation Sequence, Skeleton, Physics Asset은 그대로 재사용한다.
4. 테스트 BP의 Mesh를 이 파생 Skeletal Mesh로 바꾼다.
5. `weapon_r`에 새 무기를 붙이고 Idle·이동·공격·피격·사망·LOD를 확인한다.

### 중단 기준

무기 제거 때문에 손·팔의 웨이트가 깨지거나, 원본 무기 하나를 바꾸는 데 시간이 많이 든다면 그 캐릭터는 원래 무기를 고정 무기로 채택한다. 처음 전투 수직 슬라이스에서는 이 결정을 받아들인다.

## 완료 후 기록할 내용

| 영웅 | 원래 무기 | 해결 단계(A/B/C/고정) | 장착 본 | 새 무기 | 확인 결과 |
| --- | --- | --- | --- | --- | --- |
|  |  |  | `weapon_r` 또는 `weapon_l` |  |  |

## 관련 문서

- [[Paragon 스켈레톤 그룹화 및 리타기팅 검증]]
- [[Paragon 기반 캐릭터 구현 계획]]
