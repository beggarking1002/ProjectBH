# Greystone 3타 콤보 에디터 연결 절차

## 전제

- `ProjectBHEditor Win64 Development` C++ 빌드가 성공한 상태다.
- 첫 수직 슬라이스는 검 평타 3타만 다룬다. 방패 밀치기, 피격·사망, DataTable은 다음 단계다.
- 원본 Greystone Skeletal Mesh, Skeleton, AnimBP, Animation Sequence는 수정하지 않는다.

## 1. C++ 클래스 새로고침

1. Unreal Editor와 PIE를 종료한 상태에서 최신 C++ 빌드가 성공했는지 확인한다.
2. Editor를 다시 연다.
3. Content Browser에서 Montage Notify 메뉴에 `ANS Melee Hit Window`, `AN Combo Branch`가 보이는지 확인한다.

보이지 않으면 Editor를 완전히 종료한 뒤 다시 열고, Live Coding 없이 전체 C++ 빌드를 다시 수행한다.

## 2. Greystone 파생 Skeletal Mesh 만들기

1. `/Game/ParagonGreystone/Characters/Heroes/Greystone/Meshes`에서 실제 플레이어용 Greystone Skeletal Mesh를 찾는다.
2. 해당 Mesh를 `Content/ProjectBH/Characters/Greystone/Derived/`로 **Duplicate**한다.
3. 파생 Mesh가 원본 Greystone Skeleton과 Physics Asset을 계속 참조하는지 확인한다.
4. 이후 Socket 작업은 이 파생 Mesh에만 적용한다.

## 3. 검 Trace Socket 배치

파생 Skeletal Mesh를 열고 Skeleton Tree에서 검을 따라가는 기존 본 또는 Socket을 먼저 찾는다.

1. `FX_Sword_Bottom`, `FX_Sword_Top`이 각각 검의 하단과 상단을 따라가는지 Preview Scene에서 Idle과 공격 애니메이션으로 확인한다.
2. 두 지점이 맞으면 새 Socket을 만들지 않는다. Greystone BP에서 Sword Trace Base/Tip Name을 각각 `FX_Sword_Bottom`, `FX_Sword_Top`으로 둔다.
3. Mid Name은 비워 둔다. C++가 Base와 Tip의 중간을 자동 보간해 세 번째 Sphere Sweep을 수행한다.
4. 두 지점 중 하나가 검 형상을 제대로 따르지 않을 때만 `sword` 본에 프로젝트 전용 Mesh Socket을 만들고 그 이름을 Base 또는 Tip Name에 지정한다.

`Trace_Shield_Center`는 방패 밀치기 단계에서 사용하므로, 지금 만들어도 되지만 검 평타 테스트에는 필수가 아니다.

## 4. `ABP_Greystone` 만들기 및 Locomotion 연결

1. Greystone Skeleton을 대상으로 새 Animation Blueprint `ABP_Greystone`을 만든다.
2. 부모 클래스는 `BH Hero Anim Instance`를 선택한다.
3. Preview Mesh에 위에서 만든 파생 Greystone Mesh를 지정한다.
4. `Locomotion_BS`를 열어 빈 축 이름 `None`을 `Ground Speed`로 바꾼다.
   - Ground Speed: `0 ~ 400`
   - Direction: `-180 ~ 180`
5. State Machine 내부 `Jog` State의 `Locomotion_BS`에서 상속 변수 `GroundSpeed`, `Direction`을 각각 같은 이름 핀에 연결한다.
6. State Machine 바깥의 Anim Graph에서 `Locomotion State Machine` 포즈 출력 → `Slot` 노드 → `Output Animation Pose` 순으로 연결한다. `Jog` State 안에는 Slot을 넣지 않는다.
7. Slot Name은 `AM_Knight`와 같게 둔다. 기본값이면 둘 다 `DefaultSlot`이다.

Event Graph에 GroundSpeed/Direction을 직접 계산하거나 별도 변수를 만들지 않는다. C++ `UBHCharacterAnimInstance`가 매 프레임 값을 계산한다.

### 공격 Montage를 AnimGraph에 합성하기

공격 전용 State Machine은 현재 필요 없다. C++가 `AM_Knight`를 재생하므로, 최종 포즈 직전에 해당 Montage를 받을 Slot만 추가한다.

1. `ABP_Greystone`을 열고 `Anim Graph` 탭으로 간다.
2. 기존 `Locomotion State Machine`의 Pose 출력과 `Output Animation Pose` 사이 연결선을 끊는다. `Locomotion_BS`는 State Machine 내부의 `Jog` State에 그대로 둔다.
3. 빈 곳을 우클릭해 `Slot` 노드를 추가한다.
4. `Locomotion State Machine` Pose 출력 → `Slot`의 `Source` 입력 → `Output Animation Pose`의 `Result` 순으로 연결한다.
5. `Slot` 노드를 선택하고 Details의 `Slot Name`을 `DefaultSlot`으로 둔다.
6. Compile, Save 한다.

`AM_Knight`를 열어 Slot Track의 이름도 `DefaultSlot`인지 확인한다. Slot 이름은 Montage와 AnimBP에서 한 글자라도 다르면 공격 포즈가 출력되지 않는다.

## 5. `AM_Knight` 3타 콤보 확인

`AM_Knight`를 열고 다음을 확인한다.

> **중요:** 타임라인에 배치한 Animation Segment의 이름이나 Notify 이름은 Montage Section이 아니다. `Montage Sections` 패널에 아래 여섯 이름이 실제로 등록되어 있어야 C++가 `PlayAnimMontage(..., "Attack_A")`로 해당 위치에서 재생할 수 있다.

| 섹션 | 다음 섹션 | Notify |
| --- | --- | --- |
| `Attack_A` | `Recovery_A` | 실제 검 접촉 구간에 `ANS Melee Hit Window` |
| `Recovery_A` | 없음 | `ANS Melee Hit Window` 직후, Recovery 초반에 `AN Combo Branch` |
| `Attack_B` | `Recovery_B` | 실제 검 접촉 구간에 `ANS Melee Hit Window` |
| `Recovery_B` | 없음 | `ANS Melee Hit Window` 직후, Recovery 초반에 `AN Combo Branch` |
| `Attack_C` | `Recovery_C` | 실제 검 접촉 구간에 `ANS Melee Hit Window` |
| `Recovery_C` | 없음 | Recovery 초반에 `AN Combo Branch` (항상 종료) |

Recovery가 다음 Attack으로 자동 연결되면 안 된다. `AN Combo Branch`가 서버 입력 버퍼를 보고 다음 Attack을 시작한다.

좌클릭을 한 번만 했는데 `Attack_A → Recovery_A → Attack_B → Recovery_B → Attack_C → Recovery_C`가 전부 재생되면 Section 연결이 아직 순차 연결된 상태다. `Montage Sections` 패널의 Next Section 값을 아래와 같이 **정확히** 고친다.

| 현재 Section | Next Section |
| --- | --- |
| `Attack_A` | `Recovery_A` |
| `Recovery_A` | `None` |
| `Attack_B` | `Recovery_B` |
| `Recovery_B` | `None` |
| `Attack_C` | `Recovery_C` |
| `Recovery_C` | `None` |

특히 `Recovery_A → Attack_B`, `Recovery_B → Attack_C` 자동 연결은 제거한다. Recovery 끝의 `AN Combo Branch`가 입력 버퍼를 확인한 경우에만 C++가 다음 Attack Section을 별도로 재생한다.

### 현재 연타 입력의 동작

입력은 타격마다 하나만 예약하는 버퍼다. `Attack_A` 또는 `Recovery_A` 재생 중에 좌클릭을 한 번 이상 하면 `Attack_B`가 예약되고, `Recovery_A` 끝의 `AN Combo Branch`에서 B가 시작된다. 같은 방식으로 `Attack_B` 또는 `Recovery_B` 중 추가 입력이 있으면 C가 예약된다. 한 구간에서 여러 번 눌러도 예약은 한 번만 된다.

따라서 A가 시작된 직후에 세 번을 모두 눌러도 B만 예약된다. B가 시작된 뒤 B/Recovery_B 구간에서 한 번 더 눌러야 C까지 연결된다. 입력이 없으면 각 Recovery 끝의 `AN Combo Branch`가 콤보를 종료하고 Idle로 돌아간다.

### 빠른 콤보 전환

현재 수직 슬라이스에서는 C++를 바꾸지 않고 Montage Notify 위치로 전환 속도를 정한다. `Recovery_A`, `Recovery_B`의 기존 `AN Combo Branch`를 삭제한 뒤, 각각 **직전 Attack의 `ANS Melee Hit Window`가 끝난 직후** 또는 해당 Recovery 첫 프레임에 하나만 둔다.

- Attack 중 입력은 이미 서버에 버퍼된다.
- 이른 `AN Combo Branch`에 도달했을 때 입력이 버퍼돼 있으면, 남은 Recovery를 재생하지 않고 다음 Attack으로 즉시 전환한다.
- 입력이 없으면 Combo 상태만 종료되고 현재 Recovery 애니메이션은 끝까지 재생한 뒤 Idle로 돌아간다.

Branch보다 늦은 입력은 다음 타격으로 예약되지 않는다. 초반에는 Branch를 Recovery 시작 뒤 약 `0.10 ~ 0.15초`에 두고, 플레이 감각을 보며 앞뒤로 조정한다.

### 좌클릭해도 공격이 보이지 않을 때

Output Log에 `JumpToSectionName Attack_A ... failed for Montage AM_Knight`가 나오면, 입력은 정상적으로 들어왔지만 Montage Section 등록 또는 이름이 잘못된 것이다. 다음처럼 수정한다.

1. `AM_Knight`를 열고 Montage 편집기의 `Montage Sections` 패널을 연다. 보이지 않으면 상단 `Window` 메뉴에서 `Montage Sections`를 켠다.
2. 각 공격/회복 Animation Segment의 **시작 프레임**에 Section을 추가한다. 이름은 대소문자와 밑줄까지 정확히 `Attack_A`, `Recovery_A`, `Attack_B`, `Recovery_B`, `Attack_C`, `Recovery_C`로 지정한다.
3. `Montage Sections` 패널에서 각 Section의 Next Section을 표의 값대로 설정한다. `Recovery_A/B/C`의 Next Section은 모두 `None`이어야 한다.
4. Montage를 저장한 뒤 PIE를 다시 시작한다. Output Log에 같은 `JumpToSectionName ... failed` 경고가 더 이상 없어야 한다.

Section 오류가 사라졌는데도 동작이 보이지 않으면, 재생된 Montage가 AnimBP 최종 포즈에 합성되지 않은 것이다. `ABP_Greystone` Anim Graph를 반드시 아래처럼 만든다.

```text
Locomotion_BS → Slot (DefaultSlot) → Output Animation Pose
```

`Locomotion_BS → Output Animation Pose` 직결은 이동 포즈만 출력하므로, C++의 `PlayAnimMontage` 호출이 성공해도 공격 포즈는 화면에 나타나지 않는다. 이어서 `AM_Knight`의 Slot Track 이름과 AnimGraph `Slot Name`이 같은지 확인한다. 기본값이면 둘 다 `DefaultSlot`이다. Mesh의 Animation Mode도 `Use Animation Blueprint`, Anim Class도 `ABP_Greystone`이어야 한다.

## 6. Greystone 플레이어 Blueprint 설정

1. `ABHHeroCharacter`를 부모로 `BP_GreystoneHero`를 `Content/ProjectBH/Characters/Greystone/Blueprints/`에 만든다.
2. Mesh에 파생 Greystone Skeletal Mesh를 지정한다.
3. Anim Class에 `ABP_Greystone`을 지정한다.
4. Class Defaults에서 다음 값을 지정한다.

| 속성 | 지정값 |
| --- | --- |
| Input Config Data Asset | 현재 프로젝트의 입력 설정 Data Asset |
| Basic Attack Montage | `AM_Knight` |
| Basic Attack Damage Effect | 비워도 됨: C++ 기본 `BHGE_BasicAttackDamage`가 Health에 `-20`을 즉시 적용. 나중에 Blueprint Effect를 지정하면 그 값으로 교체 가능 |
| Combo Attack Section Names | `Attack_A`, `Attack_B`, `Attack_C` |
| Sword Trace Base/Tip Name | `FX_Sword_Bottom`, `FX_Sword_Top` |
| Sword Trace Mid Name | 비움 (Base/Tip 중간 자동 보간) |
| Sword Trace Radius | 우선 `12` |
| Max Sword Hit Actors | 우선 `1` |
| Stop Sword Trace On World | 켬 |

## 7. 입력 에셋 확인

1. `IA_BasicAttack`을 열어 Value Type이 Boolean인지 확인한다.
2. `IMC_Default`에 `IA_BasicAttack`이 있고 마우스 왼쪽 버튼이 매핑됐는지 확인한다.
3. 입력 설정 Data Asset의 Native Input Actions에서 다음 항목을 확인한다.
   - Input Tag: `InputTag.BasicAttack`
   - Input Action: `IA_BasicAttack`

## 8. 테스트 맵과 더미 설정

1. `BP_BHGameMode`를 열어 Default Pawn Class를 `BP_GreystoneHero`로 지정한다.
2. 테스트 맵에 `BP_CombatDummy`를 하나 배치한다.
3. 더미 Capsule의 Object Type이 `Pawn`이고 Query Collision이 켜져 있는지 확인한다.
4. 벽 차단을 시험할 Static Mesh Wall 하나를 더미 앞에 둔다. Wall은 Query Collision이 켜져 있고 Object Type이 `WorldStatic` 또는 `WorldDynamic`이어야 한다.

## 9. 1인 기본 확인

PIE에서 다음 순서로 확인한다.

1. 좌클릭 1회: `Attack_A → Recovery_A → Idle`
2. `Attack_A` 또는 `Recovery_A` 중 좌클릭 1회 추가: `Attack_B`로 연결
3. `Attack_B` 또는 `Recovery_B` 중 좌클릭 1회 추가: `Attack_C`로 연결
4. 더미가 `ANS Melee Hit Window` 안에서만 체력을 잃는지 확인한다.
5. 한 스윙에 체력이 한 번만 줄어드는지 확인한다.
6. 더미와 플레이어 사이에 Wall을 두고, 벽 뒤 더미가 맞지 않는지 확인한다.

Socket이 없거나 이름이 틀리면 Output Log에 sword trace point 오류가 나오고 피해가 발생하지 않는다.

## 10. 2인 PIE 검증

1. Play Settings에서 Net Mode를 `Play As Listen Server`로 둔다.
2. Number of Players를 2로 설정한다.
3. 호스트와 클라이언트 양쪽에서 서로의 Attack_A/B/C가 보이는지 확인한다.
4. 두 화면에서 더미의 체력 감소와 타격 횟수가 같은지 확인한다.

## 현재 범위 밖

- 방패 밀치기와 `Trace_Shield_Center`
- 피격 Montage와 사망 상태
- 스태미나·관통·콤보 데이터를 DataTable로 분리
- 원래 검·방패 숨김 및 보조 총기

## 관련 문서

- [[Greystone 검방패 전투 수직 슬라이스 작업 목록]]
- [[Greystone Montage Notify 배치 절차]]
- [[Greystone AnimBP Preview Mesh 문제 해결]]
