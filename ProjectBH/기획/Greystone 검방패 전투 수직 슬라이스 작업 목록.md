# Greystone 검방패 전투 수직 슬라이스 작업 목록

## 목표

Greystone의 원래 일체형 검·방패 외형을 유지한 채, 호스트와 클라이언트가 검 평타 3타·방패 밀치기·피격을 동일하게 보는 서버 권한 근접 전투를 완성한다. DataTable 기반 수치화는 핵심 전투 루프가 검증된 다음 단계로 미룬다.

이번 범위에는 플레이어에게 여러 무기를 선택시키는 UI, Greystone 원래 무기의 메시 분리, 도끼·창·석궁의 최종 구현은 넣지 않는다.

## 완료 기준

- 2인 리슨 서버에서 호스트와 클라이언트가 Greystone으로 접속한다.
- 검 평타 3타와 방패 밀치기가 지정된 애니메이션 타격 창에만 더미에 적중한다.
- 서버가 피해·가드·사망을 확정하고, 양쪽 화면에서 같은 결과를 본다.
- 핵심 전투 루프 검증 후 `DT_AttackDefinition` 행으로 피해·관통·다음 콤보를 데이터화한다.
- 스윕 Debug Draw로 칼날 추적점이 실제 검의 궤적을 따른다는 것을 확인한다.

## 작업 목록

| 순서 | 작업 | 담당 | 산출물 / 확인 기준 |
| --- | --- | --- | --- |
| 0 | 범위 확정 | 공동 | 검 평타 3타, 방패 밀치기, 피격·사망만 포함. 원래 검·방패는 유지 |
| 1 | Greystone 파생 시각 메시 준비 | 사용자 | 원본을 보존한 복제 Skeletal Mesh, 기존 Skeleton·AnimBP 정상 재생 |
| 2 | 추적 Socket 배치 | 사용자 | `Trace_Sword_Base/Mid/Tip`, `Trace_Shield_Center`가 공격 중 무기를 정상 추적 |
| 3 | 애니메이션 선정·Montage 준비 | 사용자 | 평타 1·2·3, 방패 밀치기, 피격, 사망 Montage와 콤보 연결 |
| 4 | 타격 창 Notify 배치 | 사용자 | 각 Montage에 `ANS_MeleeHitWindow`가 실제 접촉 구간에 배치됨 |
| 5 | 서버 권한 근접 판정 구현 | Codex | Multi Sphere Sweep, HitActors 중복 방지, 벽 충돌·피해 처리 |
| 6 | 전투 입력·3타 콤보 상태 연결 | Codex + 사용자 | 입력→서버 버퍼→A/B/C→Recovery 분기→Notify→판정 흐름이 동작 |
| 7 | 더미와 충돌·피격 반응 설정 | 사용자 + Codex | `CombatHitbox` 충돌, 서버 체력 감소, 피격·사망 결과 복제 |
| 8 | 호스트·클라이언트 검증 | 공동 | 각 측에서 10회 이상 공격해 중복 피해·누락·벽 관통 확인 |
| 9 | 공격·Trace DataTable 설계 | Codex | 핵심 루프 통과 후 `DT_AttackDefinition`, `DT_TraceProfile` 행 구조와 초기 데이터 |
| 10 | 밸런스·추적점 조정 | 공동 | DataTable 수치/Socket 위치 조정 내역과 근거 기록 |
| 11 | 확장 게이트 결정 | 공동 | 두 번째 무기 도입 전, Greystone 무기 분리 또는 무기 고정 클래스 유지 결정 |

## 세부 순서

### 현재 다음 작업: 3타 콤보 애니메이션 연결

DataTable과 서버 판정 구현보다 먼저, `AM_Knight`의 평타 3타·Recovery 구성을 Greystone 검+방패 콤보에 연결한다. 원본 Animation Sequence·Skeleton·AnimBP는 수정하지 않는다.

1. `/Game/ParagonGreystone/Characters/Heroes/Greystone/Meshes`와 해당 영웅의 Animation 폴더에서 검 공격 후보를 찾는다.
2. 후보마다 Greystone 원본 Skeletal Mesh로 미리 보기를 해 다음을 확인한다.
   - 검+방패 자세가 유지되는가
   - Root Motion이 없거나 기본 공격으로 쓸 만큼 이동량이 작은가
   - 1타와 2타가 자연스럽게 연결되고, 각각 회복 자세가 있는가
   - 칼날이 명확히 지나가는 타격 구간을 잡을 수 있는가
3. 선정한 두 Animation Sequence를 `Content/ProjectBH/Characters/Greystone/Animations/Combat/`에 복제한다. 이름은 `AS_Greystone_SwordShield_Light_01`, `AS_Greystone_SwordShield_Light_02`로 통일한다.
4. 아직 원본 Animation Sequence, 원본 Skeleton, 원본 AnimBP에는 Socket·Notify·Montage를 만들거나 수정하지 않는다.

완료 기준: 평타 1·2의 원본 경로와 선정 근거가 기록되고, ProjectBH 전용 복제본 두 개가 준비된다. 그 뒤 파생 Skeletal Mesh·Socket·Montage 작업을 진행한다.

### Greystone 콤보 애니메이션 현황 (2026-08-25)

`/Game/Assets/Animation`의 `AM_Knight`에 다음 공격·회복 구성이 준비돼 있다.

| 순서 | 공격 구간 | 회복/콤보 판정 구간 |
| --- | --- | --- |
| 1타 | `Attack_A` | `Recovery_A` |
| 2타 | `Attack_B` | `Recovery_B` |
| 3타 | `Attack_C` | `Recovery_C` |

동작 규칙은 다음과 같다.

- 추가 입력이 없으면 현재 `Attack` 뒤의 해당 `Recovery`까지 재생한 뒤 종료한다.
- 콤보 입력이 있으면 `Recovery_A → Attack_B`, `Recovery_B → Attack_C`로 연결한다.
- `Recovery_C`는 항상 콤보를 종료한다.

> 결정: 2026-08-25에 `Attack_C`까지 포함한 **3타 콤보**로 확정했다. DataTable은 3타 핵심 루프와 네트워크 검증 뒤에 도입한다.

### 3타 콤보 Montage·Notify 연결

`AM_Knight`의 섹션 연결은 다음 규칙을 따라야 한다.

| 구간 | 다음 섹션 | 추가 Notify |
| --- | --- | --- |
| `Attack_A` | `Recovery_A` | 실제 검 접촉 구간에 `ANS Melee Hit Window` |
| `Recovery_A` | 없음 | 마지막 프레임 부근에 `AN Combo Branch` |
| `Attack_B` | `Recovery_B` | 실제 검 접촉 구간에 `ANS Melee Hit Window` |
| `Recovery_B` | 없음 | 마지막 프레임 부근에 `AN Combo Branch` |
| `Attack_C` | `Recovery_C` | 실제 검 접촉 구간에 `ANS Melee Hit Window` |
| `Recovery_C` | 없음 | 마지막 프레임 부근에 `AN Combo Branch` |

`Recovery_A/B/C`가 다음 Attack으로 자동 연결되면 안 된다. `AN Combo Branch`가 서버의 입력 버퍼를 확인해, 입력이 있을 때만 `Attack_B/C`를 시작한다.

### 0. 범위 확정

초기 공격 세트는 아래로 제한한다.

| 행동 | 공격 Profile | 데이터 행 예시 |
| --- | --- | --- |
| 평타 1 | 검 다중 스윕 | `Warrior.SwordShield.Light.01` |
| 평타 2 | 검 다중 스윕 | `Warrior.SwordShield.Light.02` |
| 평타 3 | 검 다중 스윕 | `Warrior.SwordShield.Light.03` |
| 방패 밀치기 | 전방 Capsule 또는 방패 중심 Sphere | `Warrior.SwordShield.ShieldBash` |
| 피격·사망 | 판정 없음 | 반응 Montage |

가드·패링은 데이터와 서버 전투 구조에 고려하되, 첫 완료 기준에는 방패 밀치기 우선으로 둔다. 가드가 기존 구현에 이미 있다면 해당 흐름에 연결하고, 없다면 다음 전투 항목으로 분리한다.

### 1~4. 사용자 Unreal Editor 작업

1. Greystone Skeletal Mesh를 `Content/ProjectBH/Characters/Greystone/Derived/`로 복제한다.
2. 복제 Mesh에 Mesh Socket 네 개를 만든다.
   - `Trace_Sword_Base`: 칼날 시작점
   - `Trace_Sword_Mid`: 칼날 중간점
   - `Trace_Sword_Tip`: 칼끝보다 조금 안쪽
   - `Trace_Shield_Center`: 방패 표면 중앙
3. Preview에서 Idle과 공격 애니메이션을 재생해 Socket이 무기 형상을 따라가는지 확인한다.
4. 필요한 원본 공격 Animation Sequence를 ProjectBH 전용 경로에 복제해 Montage로 만든다.
5. 실제 적중하는 구간에 `ANS_MeleeHitWindow`를 놓는다. 첫 번째 버전은 넉넉하게 두고 Debug Draw 결과로 좁힌다.

### 5~7. Codex 개발 작업

1. `FCombatAttackRow`와 `FTraceProfileRow`를 만든다.
2. `DT_AttackDefinition`에 평타 1·2·방패 밀치기 행을 만들고, `DT_TraceProfile`에 검·방패 Profile 행을 만든다.
3. `UMeleeHitTraceComponent`를 만든다.
   - Notify Begin에서 이전 Socket 위치 저장
   - Notify Tick에서 서버 Multi Sphere Sweep
   - Notify End에서 Trace 종료
4. Server RPC에서 입력·공격 상태·스태미나·콤보 순서를 검증한다.
5. `HitActors`, `MaxHitActors`, `CleaveBudget`, 월드 충돌 처리를 넣는다.
6. 적중 결과에 따라 체력·피격·사망을 서버에서 적용하고 결과를 복제한다.
7. 개발 중에는 스윕 구간, 적중 Actor, 현재 Attack ID를 화면/로그에 표시한다.

### 8~10. 공동 검증

| 검증 항목 | 통과 조건 |
| --- | --- |
| 타이밍 | Notify 밖에서는 적이 맞지 않음 |
| 궤적 | 칼을 피한 적은 맞지 않고, 칼날이 통과한 적은 맞음 |
| 중복 | 한 스윙에 같은 적이 한 번만 피해를 입음 |
| 다중 적중 | DataTable의 관통 수만큼만 여러 적이 맞음 |
| 벽 | 벽 뒤 적이 맞지 않고, `bStopOnWorld` 규칙이 동작 |
| 네트워크 | 호스트·클라이언트 모두 같은 체력·피격·사망 결과를 확인 |
| 데이터화 | 피해/관통/콤보를 DataTable만 수정해 변경 가능 |

## 확장 게이트: 두 번째 무기 전에 답할 질문

1. 같은 Greystone 외형으로 도끼·창·석궁을 실제 플레이어가 선택해야 하는가?
2. 무기 변경이 시각 외형까지 바뀌어야 하는가?
3. 다른 Paragon 캐릭터를 무기 고정 클래스 외형으로 쓰는 편이 기획상 더 좋은가?

1, 2가 모두 예라면 Greystone의 원래 검·방패를 제거한 파생 Skeletal Mesh를 만들고 `EquippedWeaponMesh` 방식으로 옮긴다. 아니라면 Greystone은 검·방패 전사로 고정하고, 다른 무기는 클래스/캐릭터별 장비로 추가한다.

## 관련 문서

- [[무기 Trace Profile 및 Greystone 확장 방침]]
- [[근접 공격 판정 설계 초안]]
- [[Paragon 일체형 무기 제거 및 교체 절차]]
