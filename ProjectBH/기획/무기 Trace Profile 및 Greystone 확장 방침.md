# 무기 Trace Profile 및 Greystone 확장 방침

## 상태

전사 첫 수직 슬라이스용 설계 초안. Greystone의 검·방패가 캐릭터 Skeletal Mesh에 일체형인 상황을 전제로 한다.

## 결론

1. **첫 수직 슬라이스에서는 Greystone의 검·방패를 떼어내지 않는다.** 고정 외형의 검·방패 전사를 먼저 완성한다.
2. 대신 전투 코드는 본체에 붙은 무기와 나중에 장착할 별도 무기를 모두 지원하는 추상 구조로 만든다.
3. 플레이어가 두 번째 무기를 실제로 선택할 수 있게 만들기 직전에, Greystone 파생 메시에서 원래 무기를 제거하고 장착형으로 전환할지 결정한다.
4. 원래 검을 든 Greystone에게 UI상 도끼·창을 선택하게 하고 수치만 바꾸는 방식은 최종 결과물로 쓰지 않는다. 시각·공격 궤적·애니메이션의 신뢰를 해치기 때문이다.

## Trace Profile이란 무엇인가

Trace Profile은 “이 공격이 몇 대를 때리는가”가 아니라 **어디를 어떤 부피로 추적할지**를 정의하는 데이터다.

```text
Attack Definition
  ├─ 피해 / 스태미나 / Cleave / 다음 콤보
  └─ TraceProfileId ──> Trace Profile
                            ├─ 추적 대상 컴포넌트
                            ├─ 추적점(Socket/Bone) 1~3개
                            ├─ 각 추적점의 Sphere 반지름
                            ├─ 충돌 채널
                            └─ 월드 충돌 중단 여부
```

예를 들어 `Warrior.SwordShield.Light.01`은 `TP_Greystone_Sword_Sweep`을 참조한다. 이 Profile은 칼날 뿌리·중간·끝의 이전 위치에서 현재 위치까지를 매 타격 프레임 Sphere Sweep하도록 지시한다.

## 권장 데이터 구조

### DataTable: `DT_TraceProfile`

첫 프로토타입에서는 한 행에 최대 세 추적점을 두어 DataTable을 쉽게 읽고 수정하게 한다.

| 필드 | 예시 | 의미 |
| --- | --- | --- |
| `TraceProfileId` | `TP_Greystone_Sword_Sweep` | 공격 행이 참조하는 ID |
| `TraceSource` | `CharacterMesh` | 기준 컴포넌트: 캐릭터 본체 / 장착 무기 / 캐릭터 전방 |
| `Shape` | `MultiSphereSweep` | 다중 Sphere Sweep, TipSphereSweep, ForwardCapsule 등 |
| `Point1Name` | `Trace_Sword_Base` | 첫 Socket/Bone 이름 |
| `Point2Name` | `Trace_Sword_Mid` | 두 번째 Socket/Bone 이름 |
| `Point3Name` | `Trace_Sword_Tip` | 세 번째 Socket/Bone 이름 |
| `RadiusCm` | `6.0` | 각 Sphere의 시작 반지름. 디버그 후 조정 |
| `TraceChannel` | `CombatHitbox` | 적 Hurtbox만 대상으로 하는 전용 채널 |
| `bStopOnWorld` | `true` | 벽에 닿으면 해당 스윙을 막을지 여부 |

`DT_AttackDefinition`은 `TraceProfileId`와 함께 피해·가드 피해·CleaveBudget·다음 콤보를 가진다. 즉 Profile은 **형상**, Attack 행은 **게임 규칙**을 담당한다.

### 지원해야 할 두 Trace Source

| `TraceSource` | 쓰는 경우 | 위치 획득 방식 |
| --- | --- | --- |
| `CharacterMesh` | Greystone처럼 무기가 본체 메시 일체형 | 캐릭터 Mesh의 Socket/Bone Transform을 읽음 |
| `EquippedWeaponMesh` | 나중에 별도 검·도끼·창 메시를 붙인 캐릭터 | 장착 무기 Component의 Socket Transform을 읽음 |

공통 Combat Component는 먼저 `TraceSource`에 맞는 Mesh Component를 고르고, 그 안에서 `Point1~3`의 Transform을 읽는다. 따라서 Greystone을 장착형으로 바꾸더라도 C++ 스윕 로직은 바꾸지 않고 **Trace Profile 행과 Visual Data Asset만 교체**하면 된다.

## Greystone 첫 구현

### 사용자: Unreal Editor 작업

원본 Paragon 에셋을 보존하려면 Greystone의 Skeletal Mesh를 `Content/ProjectBH/Characters/Greystone/Derived/`로 복제한다. 복제본에는 원래 Skeleton을 계속 사용한다.

복제한 Skeletal Mesh에서 **Mesh Socket**을 만들어 다음 위치에 둔다. Mesh Socket은 이 Greystone 메시 하나에만 적용되므로, 같은 Skeleton을 쓰는 다른 캐릭터의 소켓 설정에 영향을 주지 않는다.

| Socket | 부모 본 | 배치 위치 |
| --- | --- | --- |
| `Trace_Sword_Base` | 원래 검을 따라가는 `weapon_r` 계열 본 | 칼날이 시작하는 가드 바로 앞 |
| `Trace_Sword_Mid` | `weapon_r` 계열 본 | 칼날 길이의 약 50% 지점 |
| `Trace_Sword_Tip` | `weapon_r` 계열 본 | 칼끝보다 약간 안쪽 |
| `Trace_Shield_Center` | 방패를 따라가는 `weapon_l` 계열 본 | 방패 표면 중앙 |

소켓 이름과 실제 부모 본 이름은 에셋마다 다를 수 있다. Preview Viewport에서 각 Socket이 공격 애니메이션 중 칼·방패를 따라가는지 확인한 뒤 저장한다.

첫 Profile은 아래 둘이면 충분하다.

| Profile | Shape | 점 | 사용 공격 |
| --- | --- | --- | --- |
| `TP_Greystone_Sword_Sweep` | MultiSphereSweep | Base/Mid/Tip, 반지름 6cm부터 시험 | 평타·강공격 베기 |
| `TP_Greystone_Shield_Bash` | ForwardCapsule 또는 Shield Center Sphere | Shield Center, 반지름/길이 별도 설정 | 방패 밀치기·가드 카운터 |

### Codex: 개발 채팅에서 구현할 내용

- `ECombatTraceSource { CharacterMesh, EquippedWeaponMesh, CharacterForward }`를 만든다.
- `FTraceProfileRow : FTableRowBase`와 `DT_TraceProfile`을 만든다.
- 서버의 Melee Trace Component가 선택된 Source Component에서 1~3 Socket 위치를 매 타격 틱 읽는다.
- Profile별 Shape에 맞춰 Multi Sphere Sweep 또는 Forward Capsule을 실행한다.
- Socket이 없으면 조용히 원점에서 판정하지 말고 디버그 오류를 내고 해당 공격을 중단한다.

## 무기 교체의 단계적 결정

| 시점 | Greystone 외형 | 시스템 상태 | 결정 |
| --- | --- | --- | --- |
| 지금: 검·방패 수직 슬라이스 | 원래 일체형 유지 | `CharacterMesh` Profile | **무기 분리 안 함** |
| 두 번째 무기 애니메이션 시험 | 원래 일체형 유지 가능 | 다른 Profile·Montage를 개발자 테스트용으로만 검증 | 플레이어 선택 UI는 만들지 않음 |
| 두 번째 무기를 플레이어가 선택 | 원래 무기 제거한 파생 Mesh 또는 장착형 캐릭터 | `EquippedWeaponMesh` Profile | 이때 분리 작업 또는 캐릭터 교체를 결정 |
| 여러 캐릭터·무기 확장 | 일체형/장착형 혼재 가능 | 각 Weapon Definition이 Visual Mode와 Profile을 지정 | 공통 전투 코드 유지 |

### 분리 작업을 해야 하는 조건

아래 세 조건이 모두 맞을 때만 Blender 파생 메시 작업을 한다.

1. Greystone이라는 **같은 외형**이 실제로 검·방패 외의 무기를 선택해야 한다.
2. 그 선택이 개발자 테스트가 아니라 플레이어에게 보이는 장비 시스템이다.
3. 다른 Paragon 캐릭터를 해당 무기 전용 클래스로 쓰는 것으로는 기획을 만족할 수 없다.

조건이 아니라면, 원래 무기를 고정한 캐릭터를 그 무기 클래스의 대표 외형으로 쓰는 편이 제작 효율과 시각 완성도 모두 좋다.

## 포트폴리오에서 보여 줄 점

- 데이터 행 하나를 바꾸어 검의 추적점 반지름, 다중 적중, 벽 충돌 규칙을 조정할 수 있다.
- 일체형 Paragon 무기라는 제약을 `CharacterMesh`/`EquippedWeaponMesh` 데이터 선택으로 흡수했다.
- 시각 에셋 제약과 전투 밸런스 데이터를 분리했기 때문에 캐릭터 추가 시 서버 전투 코드를 복제하지 않는다.
- 스윕 Debug Draw로 Base/Mid/Tip이 실제 칼날을 따라가는 모습을 제시한다.

## 관련 문서

- [[근접 공격 판정 설계 초안]]
- [[Paragon 일체형 무기 제거 및 교체 절차]]
- [[Paragon 스켈레톤 그룹화 및 리타기팅 검증]]
