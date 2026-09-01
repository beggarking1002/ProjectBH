# Attack Slot 빠른 인계

## 목표

Attack Slot이 예약되었다는 이유만으로 멀리 있는 예약자를 계속 기다리지 않고, 실제 Slot에 더 가까운 Wait Enemy가 전열을 빠르게 채우게 한다. 이전에 롤백한 역할별 Crowd Profile은 다시 사용하지 않는다.

## 구현 규칙

- 실제로 비어 있는 Attack Slot의 Wait 도착 완화 fallback을 `1.0초`에서 `0.25초`로 줄였다.
- 예약된 Attack Slot은 다음 조건을 모두 만족할 때 빠른 인계를 준비한다.
  - 현재 예약자가 `Chasing` 상태다.
  - Enemy Actor 중심과 투영 Slot 사이의 2D 직선거리 기준으로 현재 예약자가 Slot에서 `120 cm` 이상 떨어져 있다.
  - Attack Cooldown이 끝난 Wait 후보가 현재 예약자보다 Slot에 `100 cm` 이상 가깝다. 경계값은 조건에 포함된다.
  - Corridor에서는 같은 Side 후보를 먼저 사용하고, 같은 Side에 후보가 없을 때만 반대 Side를 사용한다.
  - 같은 검사 단계에서는 가장 가까운 후보를, `1 cm` 이내 동률이면 Queue Sequence가 빠른 후보를 사용한다.
  - 후보의 공격 거리 조건이 유효하다.
- 같은 후보가 모든 조건을 연속 `0.3초` 유지해야 하며, 후보가 바뀌거나 조건을 잃으면 시간을 초기화한다.
- 교환 직전에 후보→Attack Slot과 기존 예약자→후보의 Wait Slot 양쪽 완전 Nav 경로를 검증한다. 어느 한쪽이라도 실패하면 예약을 변경하지 않고 `0.25초` 뒤 다시 검사한다.
- 양쪽 경로가 유효하면 Attack/Wait 예약을 한 서버 Tick에서 교환한다. 인계 직후 Wait 계층 전체를 다시 정렬하지 않아 검증한 목적지와 실제 목적지가 달라지지 않도록 했다.
- 기존 Attack 예약자는 후보의 Wait Slot으로 이동하고 `0.75초` 동안 Attack 재진입이 제한된다.
- 같은 Attack Slot도 `0.75초` 동안 다시 인계하지 않는다.
- 공격·회복 중인 Attack Enemy는 인계하지 않는다. 해당 Player의 Slot Component에 Combat Core 탈출 중인 Enemy가 하나라도 있으면 모든 인계를 중단한다.

## 변경하지 않은 것

- Detour Crowd의 Separation, Avoidance, 이동속도
- 정상 공격 후 Attack Slot 지속 점유
- 기존 교착 watchdog과 Combat Core 탈출
- Enemy Capsule 충돌 정책

## 코드 위치

- `CombatEngagementSlotComponent.h`
  - 빠른 인계 수치와 추적 배열
- `CombatEngagementSlotComponent.cpp`
  - `RefreshFastAttackHandovers()`
  - `FindFastAttackHandoverCandidate()`

## 검증 결과와 PIE 확인 항목

- `ProjectBHEditor Win64 Development` 빌드 성공.
- PIE에서는 다음을 확인한다.
  1. Player가 크게 이동해 기존 Attack 예약자보다 Wait Enemy가 Slot에 가까워지는 상황을 만든다.
  2. 약 `0.3초` 뒤 가까운 Wait Enemy가 Attack으로, 기존 예약자가 Wait로 바뀌는지 확인한다.
  3. 공격·회복 중인 Enemy의 Slot은 교환되지 않아 Montage 위치가 흔들리지 않아야 한다.
  4. 같은 두 Enemy가 `0.75초` 안에 다시 자리를 바꾸지 않아야 한다.
  5. 실제 초록색 빈 Attack Slot은 자격 있는 Wait Enemy가 있으면 약 `0.25~0.35초` 안에 예약되어야 한다. 기본 Slot Component Tick이 `0.1초`이므로 화면상 반응은 Tick 경계만큼 달라질 수 있다.
  6. 후보가 Attack Slot에 갈 수 있어도 기존 예약자가 후보의 Wait Slot으로 갈 수 없는 배치에서는 교환이 일어나지 않아야 한다.
