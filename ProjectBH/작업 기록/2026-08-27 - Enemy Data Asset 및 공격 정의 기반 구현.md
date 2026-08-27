# Enemy Data Asset 및 공격 정의 기반 구현

## 요청 이해

- 합의한 전투 구조를 실제 코드로 진행한다.
- 이번 단계에서는 `Enemy Data Asset → Attack Definition → 현재 공격 루프`까지 연결해, 몬스터별 에셋과 공격별 수치를 분리한다.
- 사용자가 미루기로 한 `AnimNotifyState + 무기 Sweep + 가드/패리 Resolver` 전환은 이번에 억지로 포함하지 않는다. 대신 공격 정의에 확장 지점을 명시한다.

## 이번 단계의 목표

1. Enemy Data Asset은 이동 속도와 공격별 Montage/GameplayEffect 연결을 보관한다.
2. Attack Definition DataTable은 피해량, 공격 거리, 판정 범위, 회복 시간 같은 규칙과 수치를 보관한다.
3. 현재 Guardian 기본 공격은 위 데이터를 읽어 기존 단일 Notify 방식으로 동작한다.
4. 피해량은 GameplayEffect 내부 상수가 아니라 Attack Definition의 `BaseDamage`를 사용한다.

## 단계적 구조

```text
DA_EnemyConfig
  └─ 기본 공격 선택 및 Montage/GameplayEffect 연결
       └─ DT_AttackDefinition 행
            └─ 피해량·거리·각도·회복 시간
                 └─ 현재: 단일 Notify의 Target Cone 검증
                 └─ 다음: NotifyState의 Weapon Socket Sweep
                      └─ 이후: Dodge/Parry/Guard/Damage Resolver
```

## 책임 분리

- C++: 데이터 형식, 조회, 서버 검증, GameplayEffect 적용을 구현한다.
- Unreal Editor: DataTable과 Data Asset 인스턴스를 만들고 Montage/행/GameplayEffect를 연결한다.

## 구현 결과

### C++

- `FBHAttackDefinitionRow : FTableRowBase`를 추가했다.
- `UDataAsset_EnemyConfig`와 `FBHEnemyAttackConfig`를 추가했다.
- `ABHEnemy`의 공격 Montage·거리·피해·회복 시간 직접 설정 필드를 제거하고 `EnemyConfigDataAsset` 하나를 진입점으로 사용한다.
- AI의 공격 개시 거리는 기본 공격의 DataTable 행에서 조회한다.
- 공격 시작 시 선택한 `AttackId`를 해당 공격이 끝날 때까지 고정한다.
- `BHGE_EnemyBasicAttackDamage`는 고정 `-10` 대신 `Data.Damage` SetByCaller 값을 받는다.
- `BaseDamage`는 기획자가 읽기 쉬운 양수로 저장하고, 적용 직전에 체력 감소값인 음수로 전달한다.
- `Committed Target Cone`과 `Weapon Socket Sweep` 모드를 마련했다. 현재 실행 가능한 것은 `Committed Target Cone`뿐이다.
- `GuardDamage`, `bBlockable`은 다음 Combat Resolver 단계용이며 현재 피해 처리에서는 아직 사용하지 않는다.

### 검증

- Unreal Header Tool 통과
- `ProjectBHEditor Win64 Development` 빌드 성공

## 사용자가 에디터에서 할 일

에디터가 이전 C++ 클래스를 잡고 있다면 먼저 에디터를 종료하고 빌드된 프로젝트를 다시 연다.

이번 연결 대상은 다음과 같다.

- Enemy Blueprint: `/Game/Enemy/BP_BHEnemy`
- 기본 공격 Montage: `/Game/Enemy/AM_Guardian_Attack`
- 기존 AnimNotify: Montage 안의 `BH Enemy Attack Hit`

기존 Blueprint의 Montage와 공격 수치는 새 Data Asset으로 자동 이관되지 않는다. 아래 연결을 완료하기 전까지 Enemy는 추적은 해도 공격을 시작하지 않는다.

### 1. 공격 DataTable 만들기

1. `/Game/Enemy/Data` 폴더를 만든다.
2. 우클릭 → `Miscellaneous` → `Data Table`을 선택한다.
3. Row Structure로 `BHAttackDefinitionRow`를 선택한다.
4. 이름을 `DT_AttackDefinition`으로 정한다.
5. `Guardian.Basic.01` 행을 만들고 다음 시작값을 입력한다.

| 필드 | 시작값 | 현재 의미 |
| --- | ---: | --- |
| `BaseDamage` | 10 | 플레이어 체력 피해 |
| `GuardDamage` | 10 | 다음 가드 단계용 예약값 |
| `bBlockable` | true | 다음 Resolver 단계용 예약값 |
| `HitDetectionMode` | `Committed Target Cone` | 현재 구현된 단발 Notify 판정 |
| `AttackStartRange` | 150 cm | AI가 멈추고 공격을 시작하는 거리 |
| `TargetConeRange` | 225 cm | Notify 시점 최대 수평 적중 거리 |
| `TargetConeHalfAngle` | 60° | 전방 판정의 좌우 반각 |
| `TargetConeHeightTolerance` | 120 cm | 허용 높이 차이 |
| `RecoveryDuration` | 0.75 s | Montage 종료 뒤 재공격 금지 시간 |

### 2. Guardian Enemy Data Asset 만들기

1. 우클릭 → `Miscellaneous` → `Data Asset`을 선택한다.
2. Data Asset Class로 `DataAsset_EnemyConfig`를 선택한다.
3. 이름을 `DA_Enemy_Guardian`으로 정한다.
4. `MaxWalkSpeed`를 우선 `300`으로 둔다.
5. `DefaultAttackId`를 `Guardian.Basic.01`로 입력한다.
6. `Attacks`에 원소를 하나 추가하고 아래처럼 연결한다.

| 필드 | 값 |
| --- | --- |
| `AttackId` | `Guardian.Basic.01` |
| `AttackDefinition` | DataTable은 `DT_AttackDefinition`, Row Name은 `Guardian.Basic.01` |
| `Montage` | `/Game/Enemy/AM_Guardian_Attack` |
| `DamageEffect` | C++ GameplayEffect 클래스 `BHGE_EnemyBasicAttackDamage` |

`DefaultAttackId`와 `AttackId`는 철자까지 같아야 한다. `DamageEffect`가 비어 있으면 현재 C++ 기본 효과를 대신 사용하지만, 포트폴리오 데이터의 연결 관계가 보이도록 명시적으로 지정하는 편을 권장한다.

### 3. Enemy Blueprint에 연결하기

1. `/Game/Enemy/BP_BHEnemy`를 연다.
2. Class Defaults → `Config` → `Enemy Config Data Asset`에 `DA_Enemy_Guardian`을 지정한다.
3. 기존 Montage의 `BH Enemy Attack Hit` Notify는 그대로 유지한다.
4. 컴파일하고 저장한다.

이 변경 뒤에는 Blueprint Class Defaults에서 Montage와 공격 수치를 직접 입력하지 않는다. 모두 위 두 데이터 에셋을 통해 설정한다.

## 현재 판정 계약

- 공격을 시작할 때 가장 가까운 플레이어 하나를 `AttackTarget`으로 고정한다.
- Notify 순간 새 대상을 탐색하지 않고, 고정된 대상만 검사한다.
- 수평 거리, 높이 차이, Enemy 전방 벡터와 대상 방향의 내적으로 Cone 내부인지 판정한다.
- 현재 단계에는 벽이나 기둥을 확인하는 Line of Sight 검사가 없다. 따라서 벽 너머 대상도 수치 조건만 맞으면 적중할 수 있으며, Weapon Sweep 단계에서 충돌 채널과 함께 보완한다.
- 한 번의 공격에서는 Notify가 중복 호출되어도 최초 성공 피해 한 번만 적용한다.
- 대상이 사라지거나 범위 밖으로 벗어나면 해당 Notify는 빗나간 것으로 끝난다.
- Montage가 정상 종료되거나 중단되면 모두 DataTable의 `RecoveryDuration`만큼 회복 상태를 거친 뒤 다시 추적한다.

## 설정 오류 시 동작

| 오류 | 결과 |
| --- | --- |
| `EnemyConfigDataAsset` 미지정 | Enemy가 공격하지 않고 Output Log에 설정 경고를 한 번 남김 |
| `DefaultAttackId`와 배열의 `AttackId` 불일치 | 기본 공격을 찾지 못해 공격하지 않음 |
| DataTable 또는 Row Name 미지정 | 공격 정의를 읽지 못해 공격하지 않음 |
| Montage 미지정 | 공격하지 않음 |
| `Weapon Socket Sweep` 선택 | 아직 미구현이므로 공격하지 않고 경고를 남김 |
| `DamageEffect` 미지정 | C++ `BHGE_EnemyBasicAttackDamage`를 대신 사용 |

`MaxWalkSpeed`의 단위는 Unreal Character Movement 기준 `cm/s`다.

## 빠른 확인 방법

다른 피해·가드·무적 효과가 없는 플레이어와 Enemy 한 마리로 확인한다. Montage에는 `BH Enemy Attack Hit` Notify가 정확히 한 개 있어야 한다.

1. `BaseDamage`를 눈에 띄는 값인 `17`로 임시 변경한다.
2. PIE에서 한 공격당 체력이 정확히 한 번, 17 감소하는지 본다.
3. `AttackStartRange`를 `300`으로 바꾸고 150일 때보다 먼 거리에서 공격을 시작하는지 본다.
4. Montage가 끝난 뒤 `0.75초` 동안 재공격하지 않는지 본다.
5. PIE의 Net Mode를 `Play As Listen Server`, 플레이어 수를 `2`로 바꿔 서버와 클라이언트에서 동일한 체력 결과가 보이는지 확인한다.
6. Output Log에 공격 설정 경고나 SetByCaller 오류가 없는지 확인한다.
7. 시험 뒤 `BaseDamage = 10`, `AttackStartRange = 150`으로 복원한다.

다음 구현 단계에서는 `Weapon Socket Sweep`을 실제 `AnimNotifyState`와 Trace Component에 연결하고, 그 결과를 공통 Combat Resolver로 넘긴다.

## GA 도입 시점 결정

현재 단계에서는 Enemy 공격을 Gameplay Ability로 다시 작성하지 않고, 구현된 데이터 기반 공격 루프를 먼저 에디터에서 검증한다.

- 현재 플레이어와 Enemy는 `AbilitySystemComponent`, `AttributeSet`, `GameplayEffect`를 사용하지만 공격 실행 자체는 C++ 함수가 담당한다.
- `DA_InputConfig`는 플레이어 입력 매핑용이며 Gameplay Ability 목록이 아니다.
- 이번 검증의 목적은 Data Asset 연결, 공격 거리, Montage, Notify, 피해량, 회복 루프가 정상적으로 이어지는지 확인하는 것이다.
- 이 수직 슬라이스가 통과한 뒤 플레이어와 Enemy의 공격 실행을 공통 Gameplay Ability 구조로 이관한다.
- 가드·패리·회피와 상태 태그는 GA 이관 뒤 추가하는 것을 기본 순서로 한다.

따라서 현재 구조는 최종 전투 아키텍처가 아니라, 데이터 연결과 단일 공격 루프를 검증하기 위한 의도적인 중간 단계다.

## 공격 애니메이션이 보이지 않는 현상 진단

### 확인된 사실

- 최신 PIE 로그에서 Enemy의 피해가 `Health: 100 → 90`, `90 → 80`으로 반복 적용됐다.
- `BH Enemy Attack Hit` Notify가 호출돼야만 이 로그가 남으므로 Data Asset, DataTable, Montage 재생, Notify, 서버 피해 처리는 정상이다.
- `AM_Guardian_Attack`은 `DefaultSlot`을 사용한다.
- `ABP_Guardian`에는 `DefaultSlot`을 최종 포즈에 합성하는 Slot 노드가 없는 것으로 확인됐다.

### 원인

Montage는 재생되고 Notify도 실행되지만 AnimBP의 최종 포즈 경로가 Locomotion State Machine만 출력한다. 따라서 공격 Montage의 포즈는 화면에 반영되지 않는다.

### 에디터 수정

`/Game/Enemy/Animation/ABP_Guardian`의 AnimGraph에서 다음 구조로 연결한다.

```text
Locomotion State Machine
  → Slot(DefaultGroup.DefaultSlot)
  → Output Pose
```

현재 공격은 전신 공격이므로 우선 Slot 노드를 바로 연결한다. 이동과 상체 공격을 동시에 표현할 필요가 생기면 이후 `Layered Blend Per Bone`과 `UpperBodySlot` 구조로 확장한다.

## 기본 공격 애니메이션 무작위 선택

### 요청 이해

하나의 Guardian 공격 Montage에 배치된 두 공격 애니메이션을 별도 Section으로 구분하고, 공격을 시작할 때마다 서버가 Section 하나를 무작위로 선택해 모든 클라이언트가 같은 공격을 보게 한다.

### 구성 원칙

- 공격 정의 행과 피해 수치는 두 애니메이션이 공유한다.
- `DA_Enemy_Guardian`의 기본 공격 항목에 선택 가능한 Montage Section 이름을 배열로 둔다.
- 무작위 선택은 서버에서 한 번만 수행하고, 선택된 Section 이름을 Multicast로 전달한다.
- 배열이 비어 있으면 기존처럼 Montage 처음부터 재생한다.
- 진짜 무작위 선택이므로 같은 Section이 연속으로 나올 수 있다.

### 구현 결과

- `FBHEnemyAttackConfig`에 `MontageSections` 배열을 추가했다.
- 서버가 유효한 Section만 모아 하나를 무작위로 선택한다.
- 선택된 Montage와 Section 이름을 Multicast RPC로 전달하므로 서버와 모든 클라이언트가 같은 공격 애니메이션을 재생한다.
- 공격을 시작할 때 서버가 선택한 Section 이름을 Output Log에 남긴다.
- 잘못된 Section 이름은 Output Log에 경고를 남기고 선택 대상에서 제외한다.
- UHT 및 `ProjectBHEditor Win64 Development` 빌드에 성공했다.

### 사용자가 에디터에서 할 일

새 배열 필드가 보이도록 Unreal Editor를 완전히 종료한 뒤 다시 연다.

1. `/Game/Enemy/AM_Guardian_Attack`을 연다.
2. 첫 번째 애니메이션이 0초에서 시작한다면 기존 `Default` Section 헤더를 선택하고 Details의 Section Name을 `Attack_01`로 바꾼다. 같은 위치에 Section을 겹쳐 만들지 않는다.
3. 두 번째 애니메이션 시작점의 상단 Montage 트랙을 우클릭하고 `Create New Section`을 선택해 `Attack_02`를 만든다.
4. Montage Sections 패널이 보이지 않으면 상단 `Window` 메뉴에서 연다. 패널의 `Clear`를 눌러 Section 연결을 모두 해제한다. 결과적으로 `Attack_01`, `Attack_02`의 Next Section은 모두 `None`이어야 한다. 두 Section을 서로 연결하면 한 번의 공격에 두 애니메이션이 연속 재생된다.
5. 각 Section의 실제 타격 프레임에 `BH Enemy Attack Hit` Notify를 하나씩 배치한다.
6. `DA_Enemy_Guardian`을 열고 기본 공격 항목의 `Montage Sections` 배열에 원소 두 개를 추가한다.
7. 배열에 `Attack_01`, `Attack_02`를 정확히 입력하고 저장한다.

```text
Attacks[0]
  AttackId: Guardian.Basic.01
  Montage: AM_Guardian_Attack
  MontageSections[0]: Attack_01
  MontageSections[1]: Attack_02
```

PIE에서 여러 차례 공격을 지켜보고, Output Log의 `selected montage section` 기록이 실제 재생된 동작과 일치하는지 확인한다. 같은 Section이 연속으로 나오는 것은 정상적인 무작위 결과다.

### `Attack_01`만 연속 선택된 현상 진단

- 런타임 로그에서 여섯 번의 선택이 모두 `Attack_01`로 확인됐다.
- 저장된 `DA_Enemy_Guardian`을 Unreal API로 직접 읽어 `MontageSections = [Attack_01, Attack_02]`이며 두 원소가 모두 정상 저장된 것을 확인했다.
- 잘못된 Section 경고도 없으므로 에셋 연결 오류가 아니다.
- 현재 코드는 매 공격마다 독립적인 순수 무작위 선택을 한다. 두 후보 중 같은 후보가 여섯 번 연속 나올 확률은 `1/64`, 약 `1.56%`이므로 가능한 결과다.
- 플레이 체감상 두 동작이 고르게 보여야 한다면 순수 무작위 대신 Section 두 개를 섞어 모두 소진한 뒤 다시 섞는 Shuffle Bag 방식으로 변경하는 것이 적합하다.

### 무작위 선택 정책 확정

- 추가 플레이 검증에서 `Attack_02`도 정상적으로 선택되는 것을 확인했다.
- 에셋 설정과 현재 선택 로직에는 문제가 없으므로 코드를 변경하지 않는다.
- 기본 공격은 각 시도마다 독립적으로 Section을 선택하는 순수 무작위 방식을 유지한다.
- 따라서 같은 공격이 여러 번 연속으로 나오는 결과도 정상 동작으로 취급한다.
