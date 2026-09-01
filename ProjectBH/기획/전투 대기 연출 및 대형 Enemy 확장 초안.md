# 전투 대기 연출 및 대형 Enemy 확장 초안

> 상태: 기획 논의용 초안. 구현 시작 전 수치와 범위를 확정한다.

## 1. 현재 Slot·AI 안정화 판단

현재 포위 시스템은 Open, Pocket, Corridor 배치와 Attack·Wait·Holding·Pending 계층, Combat Core 탈출, 교착 복구, 빠른 Attack 인계까지 갖춘 v1로 동결한다.

- 이후에는 치명적인 끼임, 예약 유실, 영구 정지만 수정한다.
- 체감 개선을 이유로 Slot 재배치 규칙을 계속 덧붙이지 않는다.
- 새 기능은 기존 Slot 예약을 소유권과 이동 목표의 기준으로 유지한다.

## 2. 비공격 Enemy 행동

### 목표

Attack Slot 바깥의 Enemy가 멈춘 복제품이나 동시에 움직이는 군무처럼 보이지 않고, 플레이어를 압박하는 하나의 무리처럼 보이게 한다.

### 계층별 역할

| 계층 | 연출 역할 | 허용 행동 |
| --- | --- | --- |
| Wait | 곧 공격할 전열 대기자 | 플레이어 주시, 짧은 좌우 스텝, 무기 고쳐 잡기, 공격 예비 동작 |
| Holding | 전열을 밀어붙이는 중간 무리 | 앞으로 몸 기울이기, 위협 동작, 짧은 전진·후퇴, 옆 Enemy 반응 |
| Pending | 외곽에서 압력을 만드는 군중 | 느린 횡이동, 빈 공간 쪽 압축, 함성·도발, 주변 전투 관찰 |

### 행동 규칙

- 논리 Slot은 유지하고, 실제 이동 목표는 Slot 주변의 작은 허용 영역 안에서만 움직인다.
- 애니메이션만으로 표현 가능한 동작을 우선한다. 실제 위치 변화는 짧은 스텝처럼 필요한 경우에만 사용한다.
- 각 Enemy는 고정 Random Seed, 시작 지연, 재사용 대기시간을 가져 동시에 같은 동작을 하지 않는다.
- 같은 행동을 연속 재생하지 않고, 가까운 이웃과 같은 동작이 겹치면 다른 후보를 고른다.
- Slot 재배치, 승격, 피격, 공격 시작 시 대기 행동을 즉시 취소한다.
- Player 공격, Enemy 피격·사망 같은 사건을 주변 일부 Enemy가 시간차로 바라보거나 움찔하는 반응으로 확산한다.

### 첫 구현 범위

`Threat Idle`, `Threat Gesture`, `Micro Strafe` 세 종류로 시작한다. 단순 무작위 반복이 아니라 상태별 가중치와 Cooldown을 사용한다.

## 3. 상체·하체 애니메이션 분리

### 기본 정책

- Locomotion을 기본 하체 포즈로 사용한다.
- 이동 가능한 가벼운 공격과 위협 동작은 `UpperBody` Slot으로 합성한다.
- 구르기, 피격, 사망, 강공격, 발동작이 중요한 공격은 `FullBody` Slot을 사용한다.
- 공격 Animation Sequence에 실제 발 스텝이 포함되어 있으면 무조건 UpperBody로 자르지 않는다.

### 권장 AnimGraph 구조

1. Locomotion State Machine을 Cache Pose로 저장한다.
2. Cache Pose를 하체 기반 포즈로 사용한다.
3. `UpperBody` Slot 결과를 `Layered Blend Per Bone`으로 척추 시작 Bone부터 합성한다.
4. `FullBody` Slot은 최종 전신 덮어쓰기 경로로 둔다.
5. Mesh Space Rotation Blend와 Blend Depth는 Skeleton별로 Preview에서 조정한다.

Player와 Enemy는 같은 Slot 이름과 전신·상체 판정 정책을 공유하되, Skeleton별 AnimBP 연결 작업은 각각 진행한다.

## 4. 대형 Enemy Slot 정책

### 원칙

대형 Enemy를 일반 Enemy 한 명과 같은 Slot 비용으로 취급하지 않는다. 단순히 Capsule만 키우면 전열 밀도와 공격 가능 인원이 무너진다.

### 권장 v1 모델

- Enemy에 `Normal`, `Large` 크기 등급을 둔다.
- Normal은 Slot 비용 `1`, Large는 기본 Slot 비용 `2`로 계산한다.
- Large의 Attack 위치는 일반 Attack Ring보다 바깥쪽에 둔다.
- Large가 Attack을 예약하려면 중심 후보 주변에 자신의 점유 반경만큼 빈 공간이 있어야 한다. Slot Index의 앞뒤가 아니라 실제 월드 좌표 거리로 겹치는 후보를 검사한다.
- 실제 소유자는 중심 Slot 하나만 가지되, 점유 반경과 겹치는 다른 Slot은 차단 예약으로 표시한다. 이 방식은 Open, Pocket, Corridor의 서로 다른 Slot 배열에 공통으로 적용한다.
- Large가 Wait·Holding에 있을 때도 더 큰 개인 공간과 더 넓은 도착 반경을 사용한다.
- 한 Player 주변의 Large 동시 Attack 수에는 별도 상한을 둔다.

### 별도로 고려할 항목

- Capsule Radius와 Detour Crowd Agent Radius
- 큰 Capsule이 통과할 수 없는 통로를 구분하는 Nav Agent 또는 경로 필터
- 공격 사거리, Slot Acceptance Radius, Combat Core 반경
- 작은 Enemy를 밀어낼지, 우회할지, 대형 Enemy에게 우선권을 줄지
- 사망한 대형 시체가 통로와 Nav 경로를 막을지

## 5. 권장 구현 순서

1. Player·Enemy AnimGraph에 `UpperBody`와 `FullBody` 정책을 확립한다.
2. Wait·Holding·Pending용 대기 행동 선택기와 최소 세 행동을 구현한다.
3. `Normal/Large` 크기 등급과 Slot 비용·차단 예약을 구현한다.
4. 대형 Enemy 전용 Nav 통과 가능성과 Crowd 간격을 검증한다.

## 6. 첫 수직 슬라이스 완료 기준

- Player와 Enemy가 이동하면서 UpperBody 공격을 재생해도 발이 계속 움직인다.
- 전신 공격·피격·사망은 상하체가 분리되지 않는다.
- 8명 이상 대기 중일 때 같은 행동이 동시에 반복되는 군무가 눈에 띄지 않는다.
- 대기 행동 때문에 Slot을 잃거나 승격이 늦어지지 않는다.
- Large Enemy 한 명이 전열에서 Normal 두 명 분량의 공간을 점유하고 서로 겹치지 않는다.
