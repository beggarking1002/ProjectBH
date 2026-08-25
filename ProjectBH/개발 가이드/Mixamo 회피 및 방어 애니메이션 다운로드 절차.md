# Mixamo 회피 및 방어 애니메이션 다운로드 절차

## 목적

Greystone에 리타기팅할 회피·방어 애니메이션을 Mixamo에서 일관된 소스 Skeleton으로 받아, 불필요한 메시 중복과 리타기팅 오류를 피한다.

## Greystone 기준 전체 워크플로우

Greystone은 ProjectBH에서 실제로 사용할 **대상(Target) Skeleton**이고, Mixamo 캐릭터는 외형이 아니라 애니메이션을 담는 **원본(Source) Skeleton**이다.

```text
Mixamo 캐릭터 1개 (Source Skeleton 생성)
    ↓
Mixamo 애니메이션 FBX (Without Skin)
    ↓  UE에 Source Skeleton 대상으로 Animation Only 임포트
Mixamo Animation Sequence
    ↓  IK Rig + IK Retargeter
Greystone용 Animation Sequence
    ↓
Greystone Montage / AnimBP / ProjectBH 전투 데이터
```

**Mixamo의 Without Skin FBX를 Greystone Skeleton에 직접 선택해 임포트하지 않는다.** 두 Skeleton의 본 이름·계층이 다르므로 직접 할당하는 방식은 실패하거나 깨진 애니메이션을 만든다. 반드시 Mixamo Source Skeleton으로 임포트한 다음 Greystone으로 리타기팅한다.

## 처음부터 따라 하는 절차

### A. 한 번만: Mixamo Source Skeleton 만들기

1. Mixamo Characters에서 **무기·방패·소품이 없는** 기준 캐릭터 하나를 고른다. `X Bot` 또는 `Y Bot`을 권장한다.
2. 아무 Idle 클립을 선택하고 다음 설정으로 한 번만 다운로드한다.
   - `FBX Binary`
   - `With Skin`
   - 60 FPS가 있으면 60, 없으면 30
   - Keyframe Reduction `None`
3. UE에서 `Content/ThirdParty/Mixamo/Raw/Source/`에 임포트한다.
4. 첫 With Skin FBX는 `Skeletal Mesh`로 임포트하고 `Skeleton = None`으로 둔다. 결과로 Mixamo Source Skeletal Mesh와 Skeleton을 만든다. 이 Idle 클립 자체는 필수 결과물이 아니므로 Animation Sequence로 임포트하지 않아도 된다.
5. 생성된 Skeleton을 `SKEL_Mixamo_Source`처럼 알아보기 쉽게 이름 짓는다.

이 단계에서 생긴 Mixamo 캐릭터 메시와 Skeleton은 게임에 쓰지 않는다. 실제로 쓸 `sword_and_shield_block_idle`은 다음 단계에서 같은 X Bot/Y Bot을 선택해 **Without Skin**으로 따로 받고, Source Skeleton에 Animation Only로 임포트한다.

### B. 클립마다: Mixamo에서 다운로드

1. 반드시 A에서 고른 **같은 Mixamo 캐릭터**를 유지한다.
2. 원하는 회피·방어·피격·사망 애니메이션을 선택한다.
3. Roll/Dodge는 `In Place = On`, `Loop = Off`로 둔다.
4. Guard는 `In Place = On`, `Loop = On`으로 둔다.
5. 아래 설정으로 다운로드한다.
   - `FBX Binary`
   - `Without Skin`
   - A와 같은 FPS
   - Keyframe Reduction `None`

### C. 클립마다: UE에 원본 Animation Sequence로 임포트

1. `Content/ThirdParty/Mixamo/Raw/Animations/` 폴더에서 FBX를 Import한다.
2. `Import Only Animations = On`으로 둔다.
3. Skeleton에 **`SKEL_Mixamo_Source`**를 선택한다.
4. 60 FPS FBX면 UE의 기본 30 FPS 샘플링을 끄고 Custom Sample Rate를 60으로 둔다.
5. Import 뒤 생성된 Animation Sequence를 Mixamo Source Mesh로 Preview한다.

### D. 한 번만: Mixamo → Greystone 리타기팅 준비

1. Mixamo Source Skeletal Mesh에 `IKR_Mixamo_Source`를 만든다.
2. Greystone Skeletal Mesh에 `IKR_Greystone`을 만든다.
3. 두 IK Rig에서 Retarget Root를 각각 골반/Hips 계열 본으로 지정한다.
4. Spine, LeftArm, RightArm, LeftLeg, RightLeg 체인을 같은 의미끼리 만든다.
5. `RTG_Mixamo_To_Greystone` IK Retargeter를 만들고 Source=`IKR_Mixamo_Source`, Target=`IKR_Greystone`으로 지정한다.
6. Retarget Pose에서 팔·척추 기본 자세를 맞춘다. 회피는 발, Guard는 방패 팔·검 팔을 특히 확인한다.

### E. 클립마다: Greystone용 애니메이션으로 복제

1. `RTG_Mixamo_To_Greystone`을 열고 Asset Browser에서 Mixamo 원본 Animation Sequence를 고른다.
2. Preview에서 Greystone의 발 미끄러짐, 무기 손 위치, 종료 자세를 확인한다.
3. 통과한 클립만 Export/Duplicate한다.
4. 결과를 `Content/ProjectBH/Characters/Greystone/Animations/Retargeted/`에 저장한다.
5. 이 Greystone용 Sequence로 Montage를 만들고, Guard는 상체 Slot에, Roll은 전신 Slot에 연결한다.

## 파일 위치 요약

| 종류 | UE Content 경로 | 수정 여부 |
| --- | --- | --- |
| Mixamo Source Mesh/Skeleton | `ThirdParty/Mixamo/Raw/Source/` | 원본 보존 |
| Mixamo 원본 Animation Sequence | `ThirdParty/Mixamo/Raw/Animations/` | 원본 보존 |
| Mixamo IK Rig/Retargeter | `ProjectBH/Characters/Greystone/Retarget/` | ProjectBH 설정 |
| Greystone용 리타기팅 Sequence/Montage | `ProjectBH/Characters/Greystone/Animations/Retargeted/` | ProjectBH 전용 |

## 첫 검증 클립 순서

1. `MX_Roll_Fwd_InPlace` 하나
2. `MX_Guard_Loop_InPlace` 하나
3. 피격 또는 사망 하나

세 개가 Greystone에서 자연스럽게 재생되는 것을 확인하기 전에는 추가 클립을 대량 임포트하지 않는다.

## 다운로드 원칙

- Mixamo 캐릭터 하나를 **공통 소스 Skeleton**으로 정한다. 예: `X Bot`.
- 이 캐릭터는 프로젝트에서 실제 플레이어 외형으로 쓰지 않는다. Mixamo 애니메이션을 임포트하기 위한 원본 Skeleton일 뿐이다.
- 공통 소스 캐릭터는 한 번만 `With Skin`으로 받고, 각 애니메이션은 모두 같은 캐릭터를 선택한 뒤 `Without Skin`으로 받는다.
- ProjectBH의 첫 회피는 In-Place, 서버 이동 방식이므로 회피·스텝은 `In Place`를 켠다.
- Guard는 `Loop`를 켜고, Roll/Dodge는 `Loop`를 끈다.

## 지금 받을 최소 애니메이션

| 우선순위 | Mixamo 검색어 예시 | 다운로드 목적 | Loop | In Place |
| --- | --- | --- | --- | --- |
| 1 | `Combat Roll`, `Dodge` | 회피 1종 | 끔 | 켬 |
| 2 | `Shield Block`, `Block`, `Defend` | 상체 Guard Loop | 켬 | 켬 |
| 3 | `Hit Reaction`, `Stagger` | 피격 반응 보완 | 끔 | 켬 |
| 4 | `Death` | 사망 보완 | 끔 | 켬 |

첫 단계에서는 각 분류에서 **가장 자연스러운 한 개씩만** 받는다. 방향별 회피, Guard Start/End, Guard Hit를 한꺼번에 받지 않는다.

## 1. 공통 소스 캐릭터 한 번 다운로드

1. Mixamo의 Characters에서 기준 캐릭터 하나를 선택한다. 이후 모든 애니메이션 다운로드에서 같은 캐릭터를 유지한다.
2. Animation에서 아무 기본 Idle 또는 T-Pose에 가까운 클립을 선택한다.
3. Download 설정을 아래처럼 둔다.

| 옵션 | 권장값 |
| --- | --- |
| Format | `FBX Binary` |
| Skin | `With Skin` |
| Frames Per Second | `60`이 보이면 60, 없으면 30 |
| Keyframe Reduction | `None` |
| In Place | 해당 없음 |

4. 파일명을 `MX_Source_XBot_WithSkin.fbx`처럼 바꿔 둔다.

## 2. 각 회피·방어 애니메이션 다운로드

1. 같은 Mixamo 캐릭터가 선택된 상태에서 원하는 Animation을 고른다.
2. Roll/Dodge는 Preview에서 캐릭터가 앞으로 나아가지 않는지 확인하고 `In Place`를 켠다. Guard는 `Loop`를 켠다.
3. 필요하면 Trim은 실제 행동 전체가 남도록 둔다. 준비·회복 프레임을 과하게 자르지 않는다.
4. Download 설정을 아래처럼 둔다.

| 옵션 | 권장값 |
| --- | --- |
| Format | `FBX Binary` |
| Skin | `Without Skin` |
| Frames Per Second | 소스 캐릭터와 동일. 가능하면 60 |
| Keyframe Reduction | `None` |

5. 파일명 규칙을 통일한다.

```text
MX_Roll_Fwd_InPlace.fbx
MX_Guard_Loop_InPlace.fbx
MX_Hit_01_InPlace.fbx
MX_Death_01.fbx
```

Mixamo의 다운로드 UI에서 `Without Skin` 옵션은 애니메이션을 선택한 상태에서 보일 수 있다. 이 옵션은 메시를 빼고 본·애니메이션만 받는 것이므로, 동일 소스 Skeleton 기반 클립을 여러 개 관리할 때 적합하다. 60 FPS와 Keyframe Reduction `None`은 빠른 회피의 원본 키를 보존하는 데 유리하지만, 프로젝트 용량이 문제가 되면 리타기팅·검증 후 UE 압축 설정으로 최적화한다.

## 3. Unreal Engine 임포트

### 원본 Mixamo Skeleton 만들기

1. Content Browser에서 `Content/ThirdParty/Mixamo/Raw/Source/` 폴더를 만든다.
2. `MX_Source_XBot_WithSkin.fbx`를 임포트해 Skeletal Mesh와 Skeleton을 만든다.
3. 생성된 Skeleton을 `SKEL_Mixamo_Source`처럼 명확히 이름 짓는다. 원본 메시를 게임 플레이어에 쓰지 않는다.

#### 첫 With Skin FBX 권장 설정

| 임포트 섹션 | 옵션 | 값 | 이유 |
| --- | --- | --- | --- |
| Mesh | Skeletal Mesh / Import Mesh | 켬 | Mixamo 원본 Skeletal Mesh와 Source Skeleton을 한 번 만든다 |
| Mesh | Skeleton | `None` | 새 `SKEL_Mixamo_Source`를 만들기 위함 |
| Animation | Import Animations | 끔 | 이 파일은 무기 없는 Source Skeleton 생성용 Idle이다 |
| Mesh | Import Meshes in Bone Hierarchy | 끔 | 소품·무기 메시를 본으로부터 별도 처리해 뒤틀리는 문제를 피함 |
| Mesh | Update Skeleton Reference Pose | 끔 | 이후 FBX가 Source Skeleton의 기준 자세를 바꾸지 못하게 한다 |
| Mesh | Use T0 As Ref Pose | 끔 | 선택한 Idle/행동의 첫 프레임을 T-Pose 기준 자세로 오인하지 않게 한다 |
| Material | Import Materials / Import Textures | 끔 | 이 메시는 원본 리타기팅용이므로 불필요한 Material·Texture를 만들지 않는다 |
| Physics | Create Physics Asset | 끔 | 게임 플레이어로 쓰지 않는 원본 메시다 |
| Transform | Import Translation / Rotation / Uniform Scale | 기본값 | Mixamo 원본 스케일을 임의로 바꾸지 않는다 |

`Use T0 As Ref Pose`는 FBX의 0번 프레임을 Reference Pose로 대체하는 옵션이다. Mixamo에서 Idle이나 Roll을 고른 파일은 0번 프레임이 T-Pose가 아닐 수 있으므로 기본적으로 끈다. 다만 FBX가 Bind Pose 오류를 내고 임포트에 실패할 때만, 이 옵션을 켠 별도 재시도를 한다.

#### UE 5.7 Interchange 화면의 Common Skeletal Meshes and Animations 설정

첫 `With Skin` 기준 캐릭터 FBX를 임포트할 때 화면의 항목은 아래처럼 둔다.

| 화면 항목 | 설정 |
| --- | --- |
| Force All Mesh as Type | `Skeletal Mesh` |
| Import Only Animations | 끔 |
| Skeleton | `None` |
| Import Meshes in Bone Hierarchy | 끔 |
| Use T0 As Ref Pose | 끔 |
| Add Curve Metadata to Skeleton | 끔 |

무기 없는 X Bot/Y Bot을 Source로 쓰는 경우 `Import Meshes in Bone Hierarchy`는 **끈다**. ProjectBH는 Mixamo의 무기·방패·소품 메시를 사용하지 않으며, 이 옵션은 그런 본 아래 메시를 별도로 가져오는 경우에 필요하다. `Add Curve Metadata to Skeleton`도 Mixamo Raw 원본에는 쓰지 않는다.

또한 이 화면 아래의 **Skeletal Meshes** 접힘 메뉴를 열어 다음을 확인한다.

| Skeletal Meshes 항목 | 설정 |
| --- | --- |
| Import Skeletal Meshes | 켬 |
| Import Content Type | `Geometry and Skinning Weights` |
| Create Physics Asset | 끔 |
| Import Morph Targets | 끔 |

`Import Skeletal Meshes`가 꺼져 있으면 Common 설정이 맞아도 에셋이 생성되지 않는다.

아래의 **Skeletal Animations** 접힘 메뉴에서 `Import Animations`는 꺼 둔다. 이 첫 파일은 Source Skeleton을 만들기 위한 무기 없는 Idle이므로, 실제 Guard/Roll 클립은 다음 단계에서 Without Skin으로 따로 임포트한다.

### 애니메이션 임포트

1. `Content/ThirdParty/Mixamo/Raw/Animations/`에 각 `Without Skin` FBX를 임포트한다.
2. 임포트 대화상자에서 Mesh는 임포트하지 않고 Animation을 임포트한다.
3. Skeleton 선택 목록에서 `SKEL_Mixamo_Source`를 선택한다.
4. 생성된 Animation Sequence가 전부 같은 Source Skeleton을 참조하는지 확인한다.

#### 각 Without Skin 애니메이션 권장 설정

| 임포트 섹션 | 옵션 | 값 | 이유 |
| --- | --- | --- | --- |
| Mesh | Import Mesh 또는 Import Only Animations | 끔 / Animation Only | 메시를 중복 생성하지 않는다 |
| Mesh | Skeleton | `SKEL_Mixamo_Source` | 모든 Mixamo 클립을 한 Source Skeleton에 모은다 |
| Animation | Import Animations | 켬 | Animation Sequence를 만든다 |
| Animation | Animation Length | `Exported Time` | Mixamo에서 내려받은 전체 행동 구간을 그대로 쓴다 |
| Animation | Use Default Sample Rate | 끔 | UE의 기본 30 FPS 강제 재샘플을 피한다 |
| Animation | Custom Sample Rate | `60` | Mixamo에서 60 FPS로 내려받았을 때. 30 FPS 파일이면 `30` |
| Animation | Import Custom Attributes / Curves | 끔 | Mixamo 기본 클립에는 ProjectBH가 쓸 커스텀 속성이 없다 |
| Transform | Import Translation / Rotation / Uniform Scale | 기본값 | 리타기팅 전 원본 Transform을 보존한다 |

UE 5.7에서 Interchange Importer UI를 쓰는 경우에는 `Import Only Animations`와 Skeleton 선택이 별도 항목으로 보일 수 있다. 의미는 같다. **Animation Only + `SKEL_Mixamo_Source`** 조합인지 확인한다.

같은 Interchange 화면에서 `Without Skin` 애니메이션 FBX를 임포트할 때는 다음 두 항목만 첫 단계와 다르게 바뀐다.

| 화면 항목 | 애니메이션 전용 설정 |
| --- | --- |
| Import Only Animations | 켬 |
| Skeleton | `SKEL_Mixamo_Source` 선택 |

### 아무 에셋도 생성되지 않았을 때

1. 먼저 Mixamo에서 받은 첫 파일이 정말 `With Skin`인지 확인한다. `Without Skin` FBX에는 Skeletal Mesh를 만들 기하 정보가 없으므로 Source Skeleton 생성 단계에 쓰면 안 된다.
2. 첫 With Skin FBX를 다시 임포트하면서 위 Common 설정과 `Skeletal Meshes > Import Skeletal Meshes`를 모두 확인한다.
3. 그래도 생성되지 않으면 Output Log에서 `Interchange`, `FBX`, `error`, `warning`을 검색해 가장 위 오류를 확인한다.
4. Interchange가 "no data" 등으로 FBX를 인식하지 못하는 경우에만, UE 콘솔에서 아래를 입력해 이번 에디터 세션에 한해 Legacy FBX Importer로 전환한 뒤 다시 시도한다.

```text
Interchange.FeatureFlags.Import.FBX 0
```

이 명령은 프로젝트 콘텐츠를 바꾸지 않으며 에디터를 재시작하면 기본 동작으로 돌아간다. Legacy Importer에서도 첫 파일은 `Skeletal Mesh` 임포트, `Skeleton=None`, `Import Animations=끔`으로 받는다.

`Use Default Sample Rate`를 켜면 UE가 애니메이션을 30 FPS로 샘플링한다. Mixamo에서 60 FPS로 받은 회피를 보존하려면 이 항목을 끄고 Custom Sample Rate를 60으로 둔다. 반대로 Mixamo 파일 자체가 30 FPS면 Custom Sample Rate도 30으로 맞춘다.

### 임포트 직후 확인

1. Animation Sequence를 열어 길이와 프레임 수가 Mixamo Preview와 크게 다르지 않은지 확인한다.
2. Preview Mesh를 `MX_Source_XBot`으로 두고, Roll이 제자리에서 재생되는지 확인한다.
3. Guard가 Loop 경계에서 자세가 튀지 않는지 확인한다.
4. Skeleton 필드가 `SKEL_Mixamo_Source`가 아닌 별도 Skeleton이면 잘못 임포트한 것이므로, 해당 Raw 애니메이션만 삭제하고 올바른 Skeleton으로 다시 임포트한다.

## 4. Greystone으로 리타기팅

1. Mixamo Source와 Greystone에 각각 IK Rig를 만든다.
2. 팔·다리·척추 체인을 대응시키고 기준 자세를 맞춘다.
3. 회피 1개와 Guard 1개만 우선 Greystone용으로 Export한다.
4. 결과는 `Content/ProjectBH/Characters/Greystone/Animations/Retargeted/`에 둔다.
5. Greystone 검·방패의 손 위치, 바닥 미끄러짐, 회피 종료 자세를 Preview에서 확인한다.
6. 통과한 시퀀스만 ProjectBH 전용 Montage로 만들어 전투 데이터의 `MontageId`에 연결한다.

## 판정 기준

| 행동 | 통과 조건 | 실패 시 |
| --- | --- | --- |
| Roll/Dodge | 몸이 낮아지고, 착지 뒤 이동 자세로 자연스럽게 이어짐 | 다른 후보를 리타기팅. 서버 이동 거리를 모션에 맞춰 조정 |
| Guard Loop | 방패가 전방을 가리고, 검·팔이 심하게 꺾이지 않음 | 다른 Block 후보를 사용. 필요하면 외부 전투 애니메이션 팩 검토 |
| Hit/Death | 발이 과도하게 미끄러지지 않고 무기와 몸이 분리되지 않음 | Greystone 원본 반응 애니메이션 우선 |

## 하지 않을 것

- 각 클립을 `With Skin`으로 받아 같은 캐릭터 메시를 여러 번 임포트하지 않는다.
- 검·방패 등 소품이 포함된 Mixamo 캐릭터를 Source Mesh로 쓰지 않는다. Greystone의 원래 검·방패는 리타기팅 뒤 Greystone 본체에서 처리한다.
- Mixamo 캐릭터를 Greystone 대신 게임 플레이어 외형으로 바꾸지 않는다.
- 첫 회피 구현에서 Root Motion 클립과 서버 이동을 동시에 적용하지 않는다.
- 확인하지 않은 리타기팅 클립을 Animation Blueprint 전체에 바로 연결하지 않는다.

## 참고

- Adobe는 Mixamo에서 캐릭터·애니메이션을 선택한 뒤 Download로 내보내는 기본 흐름을 안내한다: [공식 안내](https://helpx.adobe.com/kr/creative-cloud/help/mixamo-rigging-animation.html)
- `Without Skin`은 메시 없이 본과 애니메이션을 받아 중복 메시를 줄이는 용도다: [Adobe Community 설명](https://community.adobe.com/questions-696/download-t-pose-model-with-no-skin-589721)

## 관련 문서

- [[Greystone 회피 및 방어 애니메이션 확보 방침]]
- [[애니메이션 에셋 소스 및 도입 기준]]
- [[Paragon 스켈레톤 그룹화 및 리타기팅 검증]]
