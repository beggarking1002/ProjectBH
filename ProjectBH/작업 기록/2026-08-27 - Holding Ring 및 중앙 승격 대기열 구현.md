# Holding Ring 및 중앙 승격 대기열 구현

> 후속 수정: 초기 배치 중 빈 Attack Slot보다 Holding이 먼저 점유되던 문제는 [[2026-08-27 - 초기 슬롯 배정과 런타임 승격 분리]]에서 해결했다.

## 요청 이해

Attack Slot 4개와 Wait Slot 8개를 초과한 Enemy가 제자리에서 멈추는 문제를 해결한다. 추가 Enemy에게 외곽 Holding 위치를 제공하고, 슬롯이 비었을 때 각 AI Controller의 갱신 순서가 아니라 서버 중앙 대기 순번에 따라 단계적으로 승격한다.

## 구현 결과

- `EBHCombatSlotType`에 `Holding`을 추가했다.
- Player 중심 500 cm 반경에 기본 16개의 Holding Slot을 추가했다.
- Holding 반대편 슬롯으로 갈 때는 기존 Combat Core 경유 규칙을 사용하되 500 cm Holding Ring 자체를 따라 선회한다.
- 최초 슬롯 요청 순서를 `EngagementQueue`에 서버 권한으로 등록한다.
- 정상 배치는 Attack 4명, Wait 8명, Holding 16명 순으로 채운다.
- Holding까지 가득 차면 추가 Enemy는 예약 없이 Pending 상태로 순번을 유지한다.
- Slot Component가 0.1초마다 다음 승격을 중앙 처리한다.
  - 도착한 Wait 중 가장 오래된 순번을 Attack으로 승격
  - 도착한 Holding 중 가장 오래된 순번을 Wait로 승격
  - 미배정 Pending 중 가장 오래된 순번을 Holding으로 배정
- 승격 대상은 현재 예약 위치에서 기본 60 cm 이내에 도착한 Enemy로 제한한다.
- 사망·경직·타깃 소실·경로 실패로 예약을 반납하면 대기 순번도 함께 제거한다.
- 역할이 승격되면 AI가 이전 MoveTo를 버리고 새 Ring 또는 Slot 목적지로 즉시 갱신한다.
- Holding과 Wait 상태에서는 공격하지 않으며 디버그 집계의 `NonAttack` 위반으로 감시한다.

## 기본값

| 항목 | 값 | 의미 |
| --- | ---: | --- |
| Attack Slot | 4 | 실제 공격 전열 |
| Wait Slot | 8 | 다음 공격 후보 |
| Holding Slot | 16 | 12명 초과 생존 Enemy의 외곽 대기 위치 |
| Holding Ring Radius | 500 cm | 외곽 대기 반경 |
| Holding Ring Angle Offset | 11.25도 | Wait Slot과 위치가 일직선으로 겹치지 않게 하는 각도 |
| Promotion Arrival Radius | 60 cm | 중앙 승격 후보가 되기 위한 현재 슬롯 도착 범위 |

## 디버그 표시

- Holding 빈 슬롯: 파랑
- Holding 점유 슬롯: 보라
- Player 집계 `H:점유/전체`: Holding 점유 수
- Player 집계 `Q`: Holding까지 받지 못한 Pending 수
- Player 집계 `NonAttack`: Wait/Holding인데 공격 상태인 규칙 위반 수
- Enemy 문자열 Slot Type: `Holding[index]`

## 빌드 검증

- Unreal Header Tool 성공
- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공

## 사용자 PIE 검증

1. Enemy 16마리를 배치한다.
2. `A:4/4`, `W:8/8`, `H:4/16`, `Q:0`이 되는지 확인한다.
3. Enemy 24마리에서는 `H:12/16`까지 이동하고 Holding 적이 공격하지 않는지 확인한다.
4. Attack Enemy 한 명을 죽여 가장 오래 기다렸고 Wait Slot에 도착한 Enemy가 Attack으로 이동하는지 확인한다.
5. 동시에 빈 Wait Slot에는 가장 오래 기다렸고 Holding에 도착한 Enemy가 이동하는지 확인한다.
6. Enemy 30마리에서는 `H:16/16`, `Q:2`가 되는지 확인한다.
7. 슬롯을 비웠을 때 `Q`가 감소하며 Pending Enemy가 Holding 위치로 이동하는지 확인한다.
8. 승격을 반복해도 `NonAttack:0`이고 같은 Enemy가 두 Ring을 동시에 점유하지 않는지 확인한다.

## 현재 제한과 다음 작업

- Pending Enemy는 Holding 자리가 생기기 전까지 현재 위치에서 정지한다. 필요하면 Holding Slot 수를 활성 Enemy 상한에 맞게 늘린다.
- 현재 중앙 우선순위는 대기 순번이 가장 중요하고, 같은 순번의 각도·거리 가중치는 아직 없다.
- Attack Slot은 정상 공격·회복 후 지속 점유하므로 주된 승격 계기는 사망·경직·경로 실패다.
- 다음 전투 확장은 선택적 전열 교대 또는 Holding/Wait에서 시작하는 텔레그래프 돌진 공격이다.
