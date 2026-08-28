# 2026-08-27 - Attack Slot 진입 교착 원인 분석

## 목적

Attack Slot을 예약한 Enemy가 Wait/Holding Enemy 사이에 끼어 실제 공격 위치로 진입하지 못하는 현상의 원인을 정리하고 다음 구현 방향을 결정하기 위한 근거를 남긴다.

## 확인된 원인

### 1. Attack 배정 순서와 실제 선두가 다름

- 초기 배치의 슬롯 종류는 AI Controller가 슬롯을 요청한 순서에 가깝게 결정된다.
- `TryReserveSlot`은 이미 Attack 배정 대상이 된 한 Enemy에게 가장 가까운 빈 Attack Slot을 고르지만, 전체 Enemy 중 Attack에 가장 빨리 도착할 네 명을 고르지는 않는다.
- 따라서 물리적으로 후열에 있는 Enemy가 Attack을 선점하고 선두 Enemy가 Wait/Holding을 받을 수 있다.

### 2. Attack 진입로와 Wait 위치가 겹침

- Attack Slot 4개: 각도 오프셋 45도이므로 45, 135, 225, 315도에 배치된다.
- Wait Slot 8개: 각도 오프셋 0도이므로 0, 45, 90, 135, 180, 225, 270, 315도에 배치된다.
- Wait Slot 네 개가 Attack Slot과 정확히 같은 방사선상에 있다.
- 외부에서 안쪽 Attack Slot으로 이동하는 Enemy의 경로를 반경 300 cm의 Wait Enemy가 막을 수 있다.
- Detour Crowd는 통과할 여유 공간이 없거나 주변 Agent가 모두 각자의 목적지를 유지하면 자동으로 통로를 만들어 주지 못한다.

### 3. 현재 Watchdog은 사후 복구만 수행함

- 속도가 10 cm/s 미만이고 목표까지 20 cm 이상 진전하지 못한 상태가 2초 지속되면 `Stalled`로 예약을 반납한다.
- 실패 슬롯은 2초간 제외되지만 Enemy 자체가 새 순번으로 다시 진입하므로 혼잡 구조가 그대로라면 비슷한 교착이 반복될 수 있다.

## 권장 해결 방향

### 1순위: 진입 통로 확보

- Wait Ring Angle Offset을 0도에서 22.5도로 바꾼다.
- Wait 슬롯이 Attack 방사선 사이에 놓여 Attack 진입로와 직접 겹치지 않게 한다.
- Holding Ring의 현재 11.25도 오프셋은 Wait와 Attack 사이를 다시 나누는 값으로 유지할 수 있다.

### 2순위: Attack Admission 도입

- Attack Slot은 단순 등록 순서가 아니라 실제 진입 준비가 된 Enemy에게 배정한다.
- 후보 조건은 Wait Ring 도착, Attack Slot까지의 경로 유효, 최근 교착 쿨다운이 아님으로 구성한다.
- 후보 중 Attack Slot까지의 Nav 경로 거리 또는 2D 거리가 짧은 Enemy를 우선한다.
- 초기 배치 종료 시에도 같은 기준으로 Attack 네 명을 한 번 재선정하면 후열 Enemy의 선점 문제를 줄일 수 있다.

### 3순위: 교착 시 중앙 교대

- Attack 예약자가 일정 시간 진전하지 못하면 전체 대기열에서 제거했다가 다시 등록하지 않는다.
- 해당 Enemy를 Wait로 강등하고, 이미 Wait 위치에 도착한 최선임 후보를 빈 Attack으로 원자적으로 승격한다.
- 강등된 Enemy에는 짧은 Attack 재승격 쿨다운을 적용해 즉시 같은 상황이 반복되지 않게 한다.

## 권장하지 않는 해결

- Attack Enemy의 Capsule Collision을 끄거나 줄이는 방식: Enemy가 서로 관통해 군중의 공간 존중 목표와 충돌한다.
- Detour Crowd 수치만 과도하게 높이는 방식: 목적지 배열이 통로를 막는 구조 자체는 해결하지 못한다.
- Attack Enemy가 다른 Enemy를 강제로 밀어내는 방식: 흔들림과 연쇄 교착이 생기기 쉬워 보조 연출 외에는 우선 적용하지 않는다.

## 관련 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.h`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`

## 현재 상태 / 다음 단계

- 분석에서 제안한 세 항목은 [[2026-08-27 - Attack Admission 및 교착 중앙 교대 구현]]에서 C++로 구현했다.
- 실제 군중 진입과 교대 품질은 PIE 검증이 필요하다.
