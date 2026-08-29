# 2026-08-28 - 공격적 추격과 지연 Formation 구현

## 목적

모든 Enemy가 Player 중심 Ring을 즉시 맞추며 슬금슬금 접근하고, Player 이동에 같은 방향으로 동시에 반응하던 문제를 줄인다. 전투권 밖에서는 빠르게 추격하고, 전투권 안에서만 Formation을 사용하며, 외곽 Ring은 지연 Anchor를 통해 시간차를 두고 따라오게 한다.

## 구현 결과

### Pursuit와 Formation 분리

- 아직 Formation에 참여하지 않은 Enemy는 `700 cm` 안으로 들어오기 전까지 Slot과 Queue를 점유하지 않고 Pursuit한다. 따라서 게임 시작 시 거리가 `700~900 cm`여도 우선 Pursuit한다.
- Pursuit는 Player 현재 속도의 `0.35초` 뒤 위치를 예측해 NavMesh 목표로 사용한다.
- Formation 참여 뒤 거리가 `900 cm` 이상 벌어지면 `LeftEngagementRange`로 Slot·Queue를 반납하고 Pursuit로 돌아간다.
- 서로 다른 진입·이탈 반경으로 경계 상태 왕복을 막는다.

### 상태별 이동 의도

| 이동 역할 | 속도 | 바라보는 방향 |
| --- | ---: | --- |
| Pursuit | 500 cm/s | 이동 방향 |
| Attack Ingress | 450 cm/s | Player |
| Wait 이동 | 350 cm/s | 이동 방향 |
| Holding/Pending 이동 | 320 cm/s | 이동 방향 |
| Slot 정지·공격 | 정지 | Player |

Pursuit와 Ring 이동은 `Orient Rotation to Movement`, Attack Ingress와 정지·공격은 `Use Controller Desired Rotation + Focus`를 사용한다.

### Initial Charge

- Initial `0.5초` 후보 수집과 혼잡 기반 Attack 선정은 유지한다.
- 임시 Wait/Holding 예약은 최종 Attack 후보를 고르기 위한 대기열·혼잡 계산에만 남긴다. 이 기간의 실제 이동 모드는 Slot 이동이 아니라 `InitialCharge`다.
- 편성 확정 전에 Player를 관통하지 않도록, Player의 예측 위치를 향한 이동 목표에서 `450 cm` Acceptance Radius에 도달하면 멈춘다. Player가 계속 움직이므로 실제 Player와의 거리는 근사값이다.

### 개체별 비동기 갱신

- 기준 주기 `0.5초`에 `±0.15초` Jitter를 적용하여 Controller별 `0.35~0.65초` 고정 주기로 분산했다.
- 최초 반복 시점도 `0.05초~해당 Controller의 갱신 주기` 범위에서 개체마다 다르다.
- Pursuit 재경로 임계값은 `100 cm`다.
- Attack·Wait·Holding/Pending Slot 재경로 임계값은 각각 `80 / 150 / 250 cm`다.

### Engagement Anchor

- Attack Ring은 Player 현재 위치를 사용한다.
- Wait·Holding·Pending은 공유 `EngagementAnchorLocation`을 중심으로 계산한다.
- Anchor는 Player 이동 `175 cm`까지 정지하고, Dead Zone을 넘으면 최대 `350 cm/s`로 따라간다.
- Anchor 이동과 Controller별 갱신 분산을 결합해 전원의 같은 프레임 방향 전환을 줄인다.
- Player와 Anchor 사이를 청록색 선, Anchor를 청록색 구체로 표시한다.

### Formation 완화

- Ring 진입 각도 허용치를 `1° -> 10°`로 완화했다.
- Ring Waypoint Acceptance Radius를 `5 -> 30 cm`로 완화했다.
- Player `500 cm` 이동 시 전체 예약을 한 번에 바꾸던 Reform은 기본 `0 cm`, 즉 비활성화했다.

## 사용자 에디터 확인

기존 Blueprint가 예전 C++ 기본값을 Override하고 있다면 다음 값을 직접 확인한다. 현재 프로젝트의 Controller Blueprint는 `/Game/Enemy/BP_BHCrowdEnemyAIController`다. 별도 Override가 없다면 새 C++ 기본값이 그대로 적용되므로 다시 입력할 필요는 없다.

### Enemy AI Controller Class Defaults

- `Target Refresh Interval = 0.5`
- `Target Refresh Jitter = 0.15`
- `Engagement Enter / Exit Radius = 700 / 900`
- `Pursuit / Attack Ingress / Wait / Holding Speed = 500 / 450 / 350 / 320`
- `Pursuit Prediction Time = 0.35`
- `Pursuit / Attack / Wait / Holding Repath = 100 / 80 / 150 / 250`
- `Initial Charge Stop Radius = 450`
- `Ring Waypoint Acceptance Radius = 30`

### Player의 Combat Engagement Slot Component

Player Character Blueprint에서 `CombatEngagementSlots` 컴포넌트를 선택하고 확인한다.

- `Ring Ingress Angle Tolerance = 10`
- `Engagement Anchor Dead Zone = 175`
- `Engagement Anchor Follow Speed = 350`
- `Reform Trigger Distance = 0`

### Enemy Locomotion Blend Space

- Ground Speed 축 최대값을 `500` 이상으로 확장한다.
- 300 이상 구간에서 Jog/Run 애니메이션이 지나치게 미끄러지면 Pursuit용 Run 샘플을 추가한다.

### Debug 표시

- Controller의 `bDrawCrowdDebug`가 켜져 있으면 머리 위 Mode와 이동 경로를 볼 수 있다.
- Player의 `CombatEngagementSlots`에서 `bDrawDebugSlots`가 켜져 있으면 Slot, Anchor, Player-Anchor 선을 볼 수 있다.
- NavMesh 투영에 실패한 Pursuit 목표는 원래 예측 위치로 한 번 이동을 요청하고, 실패하면 다음 개체별 갱신 때 다시 시도한다. NavMesh 밖을 직접 달리는 별도 Fallback은 없다.

## PIE 검증

1. 900 cm보다 먼 Enemy 머리 위 Mode가 `Pursuit`이고 초록 경로선으로 빠르게 달려오는지 확인한다.
2. 700 cm 안에서 `InitialCharge`, 이후 `Formation`으로 전환하는지 확인한다.
3. Initial 중 Enemy가 Wait 위치로 옆걸음하지 않고 약 450 cm 경계까지 전진하는지 확인한다.
4. Pursuit·Wait·Holding 이동 중 몸이 진행 방향을 보는지 확인한다.
5. Player가 100~150 cm 움직일 때 외곽 대형이 즉시 따라 움직이지 않는지 확인한다.
6. Player가 계속 달릴 때 청록 Anchor와 외곽 Enemy가 시간차를 두고 따라오는지 확인한다.
7. Attack Enemy는 외곽보다 빠르게 Player를 따라가고 기존 공격 루프를 유지하는지 확인한다.
8. Player가 900 cm 이상 탈출하면 Enemy가 Slot을 반납하고 Pursuit로 전환하는지 확인한다.
9. Attack 4명 상한, Wait/Holding 승격, 교착 교대와 사망 슬롯 반납이 유지되는지 확인한다.
10. Wait/Holding에서 Attack으로 승격될 때 지연 Anchor 중심에서 Player 중심으로 목표가 바뀌며 갑자기 꺾이거나 순간적으로 뭉치지 않는지 확인한다.

### 관찰 편향을 줄이는 검증 순서

1. 먼저 Debug 표시를 모두 끄고 `정지 5초 -> 횡이동 5초 -> 후퇴 5초` 영상을 기록한다.
2. `700 cm` 진입부터 첫 공격 시작까지 걸린 시간을 잰다.
3. Player 횡이동 직후 `0.2초` 안에 방향을 바꾼 외곽 Enemy 수를 센다. 전원이 같은 순간에 꺾이지 않아야 한다.
4. Player가 `150 cm` 이하로 짧게 움직일 때 Wait/Holding 전체가 함께 이동하지 않는지 확인한다.
5. 그 다음에만 Debug를 켜서 문제가 Pursuit, InitialCharge, Formation 중 어디서 발생했는지 진단한다.

## 빌드 검증

- UE 5.7 UnrealHeaderTool 성공
- `BHCrowdEnemyAIController.cpp`, `CombatEngagementSlotComponent.cpp` 컴파일 성공
- `UnrealEditor-ProjectBH.dll` 링크 성공
- 결과: `Succeeded`

## 관련 파일

- `Source/ProjectBH/AI/BHCrowdEnemyAIController.h/.cpp`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h/.cpp`
- [[몬스터 이동 시스템 규칙]]
- [[2026-08-28 - 몬스터 압박감과 동기화 이동 진단]]
