# Corridor Row별 동적 Lane 배치

> 문서 성격: 구현이 끝난 변경의 인수인계 및 PIE 검수 기록이다. 새 구현 요청서가 아니다. 코드 기준값의 원본은 `CombatEngagementSlotComponent.h`, 동작 구현은 같은 이름의 `.cpp`다.

## 작업 목적

- Corridor의 Wait·Holding·Pending을 진입 순간에 고정된 2 Lane 종대에서 분리한다.
- 통로의 각 거리대가 실제로 수용할 수 있는 폭만큼 Enemy가 공간을 조밀하게 채우게 한다.
- 한 Side·Lane 대기열이 비었다는 이유로 안쪽에 빈 Slot이 남는 구조를 제거한다.

## 구현 규칙

### Row 생성

- 논리 기준은 Player Actor 위치다. `CorridorFormationRearDirection`과 그 반대 방향을 종방향 `Side 0/1`, 이에 수직인 방향을 한 Row 내부의 횡방향 Lane으로 사용한다.
- Wait·Holding·Pending의 실제 Side별 시작 중심은 `Player 위치 + 해당 Side 방향으로 투영한 Engagement Anchor 지연의 양수 부분`이다. 따라서 플레이어 이동 중에는 기존 외곽 대형처럼 한 박자 늦게 따라가되, 통로 횡방향으로는 Anchor 오차를 상속하지 않는다.
- 한 거리 단계마다 Side 0과 Side 1에 Row 하나씩을 만든다. 즉 `Row Index 0`은 Player 양쪽에 같은 거리의 Row 두 개를 가진다.
- Wait는 `Attack Ring Radius + Corridor Layer Gap`에서 시작한다.
- Holding은 마지막 Wait Row 바깥에서, Pending은 마지막 Holding Row 바깥에서 시작한다.
- Row 간격은 `Corridor Row Spacing = 100 cm`, 계층 간격은 `Corridor Layer Gap = 100 cm`다.
- Wait와 Holding의 고정 예약 용량은 각각 `8개`, `16개`다. 필요한 Slot 수가 채워질 때까지 Row를 바깥으로 늘린다.
- 한 계층은 최소 `64개 Row`, 또는 필요 Slot 수의 `2배` 중 큰 수까지 유효 Row를 탐색한다. 그 안에서 필요한 수를 만들지 못하면 새 Layout 전체를 확정하지 않는다.
- Pending은 무제한 배열을 미리 만들지 않는다. 요청된 Pending Index까지 같은 Row 규칙으로 그때 계산한다.

### Row별 Lane 계산

- 각 Row 중심에서 Corridor 축의 수직 방향으로 양쪽 최대 `350 cm`를 Navigation Raycast해 실제 횡단 폭을 측정한다.
- 양쪽 벽에서 `Corridor Agent Radius = 45 cm`를 제외한 중심 이동 가능 폭을 사용한다.
- 한쪽에 NavMesh 경계가 없으면 해당 방향은 검사 상한 `350 cm`를 폭 끝으로 사용한다. 여기서 벽은 물리 충돌 Mesh가 아니라 Navigation Raycast가 만나는 NavMesh 경계다.
- 사용 가능 폭을 `W = 왼쪽 거리 + 오른쪽 거리 - 2 × 45 cm`로 두고 `Lane Count = clamp(1 + floor(W / 95 cm), 1, 4)`로 계산한다. `W < 0`이면 해당 Side Row는 무효다.
- Lane은 두 유효 폭 끝의 중점에 정렬한다. NavMesh가 Agent Radius를 이미 반영해도 벽과 Capsule 사이 시각적·이동 여유를 보수적으로 확보하려고 `45 cm`를 추가 제외한다.
- NavMesh 투영 전후의 2D 차이가 `35 cm`를 넘는 후보는 제외한다.
- 일부 후보만 투영에 실패하면 남은 후보의 기존 Lane Index를 유지한다. 실패한 자리를 다시 나눠 중앙 정렬하지 않는다.

### 채우는 순서

- 가까운 Row를 모두 채운 뒤 다음 Row를 사용한다.
- 같은 Row 안에서는 중앙에 가까운 Lane을 먼저 사용하고 축 양쪽 Side를 교대로 펼친다.
- `Row 0`은 Side 0부터, `Row 1`은 Side 1부터 시작하고 이후 홀짝으로 반복한다. 짝수 Lane에서 중앙까지 거리가 같으면 낮은 물리 Lane Index를 먼저 사용한다.
- 예를 들어 양쪽 Row가 각각 2 Lane이면 Row 0의 순서는 `S0L0 → S1L0 → S0L1 → S1L1`이고, 다음 거리 Row는 Side 1부터 같은 방식으로 펼친다. 양쪽 Lane 수가 다르면 존재하는 후보만 건너뛰지 않고 추가한다.
- Wait와 Holding 예약 배열은 Queue Sequence 순으로 낮은 Slot Index부터 연속 압축한다.
- Pending Index도 Side·Lane별로 건너뛰지 않고 전체 미예약 Queue Sequence로 계산한다.
- Queue Sequence는 Hero의 Slot Component가 등록 시 한 번 부여하는 단조 증가 고유값이다. 값이 작은 Enemy가 오래된 요청자이며 동률은 발생하지 않는다.
- 예약 해제, 승격, 무효 Actor 정리와 새 Row 토폴로지 확정 때 재압축할 수 있다. 단순 좌표 이동은 Slot Index를 바꾸지 않는다.

### 안정화

- 토폴로지는 배열 길이와 각 Slot의 `Side Index`, `Row Index`, `Lane Index`, 해당 Row의 유효 `Lane Count` 조합이다.
- 토폴로지가 달라지면 새 후보 전체가 `Corridor Row Layout Commit Delay = 0.35초` 동안 연속으로 같아야 확정한다. 중간에 다른 조합이 나오면 대기시간을 0으로 되돌린다.
- 토폴로지가 같고 월드 좌표만 이동한 경우 Slot Index를 바꾸지 않고 위치만 갱신한다.
- 최초 유효 Layout은 대기 없이 즉시 확정한다. 이후 새 토폴로지는 Wait와 Holding을 한 번에 교체하고 예약을 Queue Sequence로 재압축한 뒤 Formation Revision을 올린다.
- 한 Side Row 조회가 실패해도 반대 Side와 이후 Row로 필요한 수를 만들 수 있으면 후보 Layout을 완성한다. 전체 필요 수를 만들지 못하거나 중심 경로 조회가 실패하면 현재 확정된 Layout을 제거하지 않는다.

### 승격

- Holding에서 Wait는 자기 Holding Slot `60 cm` 안에 도착한 전체 후보, Pending에서 Holding은 전체 미예약 후보의 Queue Sequence로 처리한다.
- Wait에서 Attack은 Wait Slot이 실제로 놓인 Corridor Side와 같은 Attack Side만 사용한다.
- 같은 Side 후보가 여러 명이면 자기 Wait Slot `60 cm` 안에 도착하고 재공격 Cooldown이 끝났으며 현재 Enemy 위치에서 Attack Slot까지 완전하고 비부분적인 Nav 경로가 있는 가장 오래된 Sequence를 선택한다.
- 같은 Side에 조건을 통과한 후보가 없으면 해당 Attack Slot은 기다린다. 이번 단계에서는 반대 Side 후보를 Player 너머로 승격하지 않는다.

## 디버그

- 공간 문자열의 `AttackLanes`는 기존 Attack 채널 선호 Lane 수다.
- `RowLanes`는 현재 확정된 Wait·Holding Row에서 가장 많은 실제 Lane 수다.
- Pending Layout은 요청 Index까지만 임시 계산하므로 `RowLanes` 집계에는 포함하지 않는다. 확정된 Wait·Holding Row가 없으면 `0`이다.
- 기존 청록색 Wait, 파란색 Holding, Pending 구체가 동적 Row 결과를 그대로 표시한다.

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`
- `ProjectBH/기획/몬스터 이동 시스템 조작 및 수치 치트시트.md`
- `ProjectBH/작업 기록/2026-08-30 - Corridor 구현 문제 재정의.md`

## 빌드 검증

- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공
- UnrealHeaderTool, C++ 컴파일, DLL 링크 성공
- Visual Studio 2022 컴파일러가 엔진의 선호 버전이 아니라는 기존 경고만 발생했다.

실행한 빌드:

```powershell
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat' ProjectBHEditor Win64 Development 'C:\UE5\ProjectBH\ProjectBH.uproject' -WaitMutex -NoHotReloadFromIDE
```

## PIE 확인 항목

1. `bh.Debug.Enabled 1`, `bh.Debug.Slots 1`을 켠다.
2. 좁은 구간의 `RowLanes`와 넓은 구간의 `RowLanes`가 실제 폭에 따라 달라지는지 확인한다.
3. Wait 8명과 Holding 16명을 채웠을 때 안쪽 Row에 빈 Slot을 남긴 채 바깥 Row가 점유되지 않는지 확인한다.
4. 폭 경계에서 조금 움직였을 때 Lane 수가 매 프레임 왕복하지 않고 약 `0.35초` 뒤 확정되는지 확인한다.
5. Corridor 양쪽 방향에 Slot이 생성되고 홀수 인원에서도 한쪽에만 계속 몰리지 않는지 확인한다.
6. Wait에서 Attack으로 승격할 때 Player를 가로질러 반대 Side Attack Slot으로 이동하지 않는지 확인한다.
7. 통로가 굽거나 NavMesh가 끊긴 곳에서 기존 Layout이 순간적으로 전부 사라지지 않는지 확인한다.

### 합격 판정의 독립성

- `RowLanes`, Slot 구체와 예약 수는 구현 자체가 만든 내부 진단값이므로 그것만으로 합격 처리하지 않는다.
- 폭이 알려진 테스트 통로에서 레벨의 실제 횡단 폭을 별도로 재고 `clamp(1 + floor((폭 - 90) / 95), 1, 4)`의 예상 Lane 수와 비교한다.
- 조밀 배치는 예약 배열이 아니라 실제 Enemy Actor 위치를 기록해, 안쪽 Row 허용 반경 밖에 빈자리가 있는데 바깥 Row에 정착한 Actor가 있는지 확인한다.
- 반대 Side 횡단 여부는 승격 전후 Enemy Actor 궤적과 Player Capsule 중심의 상대 위치를 영상 또는 Transform 로그로 확인한다.
- `0.35초` 안정화는 폭 경계를 넘은 시각과 실제 Slot 목적지가 재색인된 시각을 별도 타임스탬프로 기록해 확인한다.

## 남은 범위

- Attack Slot 360도 유효 후보 생성은 다음 단계다.
- 동적 Side 전환과 Player Combat Core 우회도 다음 단계다.
- 실제 Enemy 혼잡에 따른 횡단면 수용량 감소는 아직 반영하지 않는다.
