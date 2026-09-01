# Pocket 신속 포위 및 도달 불가 Attack 인계

## 입력 자료

- `bandicam 2026-09-01 18-46-40-294.mp4`

## 관찰

- Player가 한 Pocket에서 빠져나와 다른 Pocket으로 이동한 뒤 슬롯 좌표는 새 위치에 생성되지만, 이전 Pocket 방향에서 따라온 Enemy의 예약 소유권이 오래 유지된다.
- 반대쪽 Attack Slot 하나가 구석에 갇힌 먼 Enemy에게 계속 예약되어 가까운 Wait Enemy가 전열을 채우지 못한다.
- 기존 빠른 인계는 새 Wait 후보뿐 아니라 기존 Attack 점유자도 후보의 Wait Slot까지 완전한 경로가 있어야 했다. 구석에 갇힌 점유자가 이 검사에 실패하면 자신이 갈 수 없는 Attack Slot까지 계속 붙잡았다.
- 기존 교착 교대는 Wait 도착을 요구했고 Pocket에서는 교대 직후 두 Controller에 즉시 예약 변경을 통지하지 않았다.

## 수정 규칙

### Pocket 신속 포위

- Pocket 진입, Pocket 열린 방향 `15°` 변화, Player 또는 Engagement Anchor `75 cm` 이동 시 신속 재편을 시작한다.
- 신속 재편 유지시간은 `1.5초`다.
- 이 구간의 빠른 Attack 인계는 거리 우위를 `100 cm → 25 cm`, 후보 유지시간을 `0.3초 → 0.1초`로 줄인다.
- Wait·Holding 같은 계층의 거리 절감 재편을 즉시 실행한다.
- Pocket Attack 같은 계층 교환은 완전한 Nav 경로를 기준으로 검사한다.
- 공격·회복 중인 Attack 점유자는 계속 잠근다.

### 도달 불가 Attack 인계

- Attack Move 요청 실패, Path Following 실패, `1.5초` 미진행을 모두 중앙 인계 사유로 통일한다.
- Wait Slot 도착 여부와 무관하게 해당 Attack Slot까지 완전한 경로가 있는 Wait 후보를 검사한다.
- Nav 경로와 혼잡 점수가 가장 낮은 후보가 Attack Slot을 즉시 받는다.
- 기존 Attack 점유자가 후보의 Wait Slot까지 갈 수 있으면 Wait로 후퇴한다.
- 그 경로도 없으면 기존 점유자는 Pending으로 내려가며 Queue Sequence는 보존한다.
- 두 Controller는 같은 처리에서 예약 변경 통지를 받아 다음 프레임에 새 목적지로 이동한다.

## 조정값

### CombatEngagementSlotComponent

| 항목 | 기본값 | 의미 |
| --- | ---: | --- |
| Pocket Rapid Reform Duration | 1.5 s | 신속 재편 유지시간 |
| Pocket Rapid Handover Distance Advantage | 25 cm | 신속 재편 중 필요한 Wait 거리 우위 |
| Pocket Rapid Handover Delay | 0.1 s | 신속 재편 중 후보 확정시간 |

### BHCrowdEnemyAIController

| 항목 | 기본값 | 의미 |
| --- | ---: | --- |
| Attack No Progress Timeout | 1.5 s | 보행 종류와 무관한 Attack 이동 미진행 한계 |

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h/.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.h/.cpp`
- [[몬스터 이동 시스템 규칙]]

## 검증 상태

- `ProjectBHEditor Win64 Development` UHT, 컴파일, 링크 성공.
- 영상과 같은 Pocket→이동→Pocket 재현에서 반대편 Attack Slot이 `0.1~1.5초` 안에 가까운 도달 가능 Wait에게 인계되는지 PIE 확인이 필요하다.
- 갇힌 기존 Attack 점유자가 Wait 경로도 없을 때 Pending으로 내려가고 해당 Attack Slot의 예약선을 더 이상 붙잡지 않는지 확인해야 한다.
- 공격·회복 중인 Attack 점유자의 Montage가 재편으로 중단되지 않는지 확인해야 한다.

## 19:15 스크린샷 재검증 후 추가 수정

벽을 등진 Pocket에서 문제가 유지된 스크린샷의 Enemy 상태에 `Route:CoreEscape`와 반복되는 높은 `Reform` 횟수가 함께 확인됐다.

- 기존에는 한 Enemy라도 `CoreEscape` 중이면 모든 Attack 빠른 인계를 전역 중단했다. 이 차단을 제거하여 반대쪽 Attack Slot의 인계가 계속 진행되게 했다.
- 기존 Controller는 같은 Slot의 Formation Revision을 받아도 `bHasRequestedSlotMove`와 Watchdog 표본을 모두 초기화했다. 같은 Slot 종류·인덱스라면 강제 경로 갱신만 예약하고 진행 표본을 보존하도록 분리했다.
- 실제 Slot 종류·인덱스 변경 또는 Move Goal의 의미 있는 변화에서는 기존대로 Watchdog을 초기화한다.
- 추가 수정 후 `ProjectBHEditor Win64 Development` UHT, 컴파일, 링크 성공.
