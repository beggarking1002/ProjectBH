# Greystone 회피 및 방어 애니메이션 확보 방침

## 현재 상태

현재 프로젝트 `Content`에서 Paragon: Greystone 팩의 Animation Sequence를 확인할 수 없다. 따라서 원본 팩 안에 회피·방어·가드 관련 클립이 실제로 있는지는, 팩을 ProjectBH에 추가한 뒤 판단한다.

애니메이션이 부족하더라도 회피·방어의 게임플레이 시스템을 막지 않는다. 행동 규칙과 애니메이션 소스를 분리해 구현한다.

## 결론

첫 Greystone 수직 슬라이스에는 아래 두 행동만 추가한다.

| 행동 | 초기 범위 | 애니메이션 최소 요건 | 시스템 우선순위 |
| --- | --- | --- | --- |
| 회피 | 이동 입력 방향으로 1회 구르기/스텝 | In-Place 회피 클립 1개 | 높음 |
| 방어 | 방패를 든 상태의 Hold Guard | Guard Loop 1개 | 높음 |

방패 가드 시작·해제, 가드 피격, 가드 브레이크, 4방향 전용 회피는 첫 동작 확인 뒤에 추가한다.

## 애니메이션 확보 우선순위

### 1. Greystone 원본 팩 확인

가장 먼저 Greystone의 Animation 폴더에서 아래 키워드로 검색한다.

```text
Dodge, Roll, Evade, Dash, Avoid, Block, Guard, Defend, Shield, Hit, Stagger
```

원본에 적합한 클립이 있으면 그대로 쓰거나 ProjectBH 전용 Montage로 복제한다. 검·방패가 일체형이므로 이 방법이 가장 자연스럽다.

### 2. 직립 Paragon 영웅에서 기증받아 리타기팅

Greystone 원본에 없을 때, 먼저 같은 직립 휴머노이드 그룹의 모션을 찾는다.

- 회피: Kallari 등 기동성 있는 직립 영웅의 Roll/Dodge/Backstep 후보
- 방어: 방패 또는 양손 방어 자세가 있는 직립 영웅의 Block/Guard 후보

각 클립은 Greystone용 IK Rig/IK Retargeter로 **복제**한다. 기증자의 Animation Blueprint 전체를 그대로 Greystone에 연결하지 않는다.

선택 기준은 “이름”보다 다음 세 가지다.

1. 회피는 몸 중심이 낮고 착지 뒤 이동 자세로 자연스럽게 이어지는가
2. 방어는 오른손 검·왼손 방패의 실루엣과 팔 각도가 맞는가
3. 리타기팅 뒤 손·무기·척추가 심하게 깨지지 않는가

### 3. 외부 애니메이션을 임시 또는 최종 후보로 도입

1, 2가 모두 실패할 때 Mixamo·Fab·mocap 라이브러리에서 In-Place `Combat Roll`, `Dodge`, `Shield Block` 계열을 확보하고 Greystone에 리타기팅한다.

- 임포트 원칙: 캐릭터는 최초 한 번만 With Skin, 애니메이션은 Without Skin, 회피는 In Place 우선
- 원본 외부 애니메이션은 `Content/ThirdParty/<Source>/Raw/`에 보존
- Greystone용 리타기팅·Montage는 `Content/ProjectBH/Characters/Greystone/Animations/`에 둠

## 시스템 설계: 애니메이션이 없어도 먼저 고정할 규칙

### 회피

첫 구현은 Root Motion 의존 없이 **서버가 이동 거리와 무적 구간을 제어하고, In-Place 애니메이션은 시각 표현으로 재생**한다. 이렇게 하면 리슨 서버에서 판정이 일관되고, 다른 에셋의 회피 클립으로 바꿔도 이동 규칙이 유지된다.

| 데이터 항목 | 설명 |
| --- | --- |
| `DodgeDuration` | 행동 전체 시간 |
| `DodgeDistance` | 서버가 이동시킬 거리 |
| `IFrameStart`, `IFrameEnd` | 무적 판정 구간 |
| `StaminaCost` | 회피 비용 |
| `Cooldown` | 연속 회피 제한 |
| `DirectionPolicy` | 입력 방향 우선, 없으면 전방 또는 후방 |
| `MontageId` | 시각 재생 애니메이션 |

최초에는 방향별 클립 네 개를 요구하지 않는다. 입력 방향으로 캐릭터를 돌리거나 이동시키고 회피 클립 하나를 쓴다. 나중에 전·후·좌·우 전용 모션을 추가하면 `MontageId`만 방향별로 분기한다.

### 방어

방어는 단발 애니메이션보다 상태다. 첫 구현은 **Guard Loop 하나를 상체에만 재생**하고 하체 이동은 Locomotion이 유지되게 한다.

| 단계 | 첫 구현 | 확장 |
| --- | --- | --- |
| 시작 | 입력 즉시 Guard 상태 + Loop 재생 | Guard Start Montage |
| 유지 | Layered Blend per Bone으로 상체 Guard Loop | 이동 방향별 Guard BlendSpace |
| 적중 | 서버가 방어 방향·가드 수치부터 판정 | Guard Hit, 반동, 스태미나 소모 |
| 해제 | 입력 해제 시 Blend Out | Guard End Montage |
| 실패 | 가드 수치 0 또는 특정 공격 | Guard Break Montage |

방패 가드라면 애니메이션이 부족해도 공격 방향과 방패 전방 각도에 따른 서버 방어 판정을 먼저 정확히 만든다. 하지만 포트폴리오 시연 전에는 최소 Guard Loop를 확보해 시각 피드백을 맞춘다.

## 사용자: Unreal Editor 작업

1. Greystone 팩 설치 뒤 후보 Animation Sequence를 검색하고 표에 기록한다.
2. 각 후보를 Preview에서 Greystone 검·방패와 함께 재생한다.
3. 기증자 클립을 쓸 경우 Source/Target IK Rig, Retargeter, 기준 자세를 설정하고 회피 1개·가드 1개만 우선 복제한다.
4. 회피 Montage와 Guard Loop Montage를 ProjectBH 전용 경로에 만든다.
5. Guard Loop를 AnimBP의 Slot에 넣고 `Layered Blend per Bone`으로 spine 상체부터 블렌딩한다.
6. 회피·가드 시작/종료용 Notify를 실제 프레임에 배치한다.

## Codex: 개발 채팅 작업

1. 서버 권한 `Dodge` 상태, 거리 이동, 무적 창, 스태미나·쿨다운을 구현한다.
2. 서버 권한 `Guard` 상태와 전방 각도·가드 수치·가드 성공/실패 판정을 구현한다.
3. 방어·회피 DataTable 행과 Montage ID 연결 구조를 구현한다.
4. 회피/가드 상태가 공격 Trace, 피격, 사망 상태와 충돌하지 않도록 전투 상태 전이를 구현한다.
5. 호스트·클라이언트에서 회피 무적·가드 결과가 같은지 검증한다.

## 결정 기준

| 결과 | 다음 행동 |
| --- | --- |
| Greystone 원본에 회피와 Guard Loop가 있음 | 원본 기반으로 바로 진행 |
| 회피만 원본에 없음 | Paragon 기증자 → 외부 회피 클립 순서로 확보 |
| Guard Loop만 없음 | 방패 실루엣이 맞는 기증자 우선. 맞지 않으면 외부 Shield Block 확보 |
| 리타기팅 뒤 검·방패가 심하게 깨짐 | 해당 클립 폐기. Gameplay 규칙은 유지하고 다른 소스를 시험 |

## 관련 문서

- [[Greystone 검방패 전투 수직 슬라이스 작업 목록]]
- [[Paragon 스켈레톤 그룹화 및 리타기팅 검증]]
- [[애니메이션 에셋 소스 및 도입 기준]]
- [[Mixamo 회피 및 방어 애니메이션 다운로드 절차]]
