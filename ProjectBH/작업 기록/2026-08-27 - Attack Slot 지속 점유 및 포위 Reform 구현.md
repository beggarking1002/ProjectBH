# Attack Slot 지속 점유 및 포위 Reform 구현

## 요청 이해

공격 한 번마다 전열과 후열이 자리를 교환하는 동작을 없앤다. 정상적인 공격과 회복은 같은 Attack Slot에서 반복하고, 플레이어가 포위를 크게 벗어났을 때만 의도적으로 포위 대형을 재편성한다. 시체 누적형 Enemy Pool은 별도 후속 단계로 남긴다.

## 구현 결과

- `ABHEnemy::FinishAttackRecovery`가 정상 회복 후 Attack Slot을 반납하지 않는다.
- 사망, 경직, 타깃 상실, 경로 실패, 2초 교착은 기존처럼 슬롯을 반납한다.
- `UCombatEngagementSlotComponent`에 기본 500 cm의 `ReformTriggerDistance`를 추가했다.
- 플레이어가 마지막 Reform 기준점에서 500 cm 이상 이동하면 Attack Ring과 Wait Ring을 각각 재편성한다.
- 기존 Attack/Wait 역할은 유지하고, 같은 Ring 안에서 현재 Enemy 위치와 슬롯 위치 사이의 이동 거리가 짧아지도록 가장 가까운 쌍부터 다시 예약한다.
- NavMesh에 투영할 수 없는 슬롯이 하나라도 있으면 해당 Ring의 기존 예약을 유지해 예약 유실을 막는다.
- Reform revision을 AI Controller가 감지하면 기존 MoveTo를 갱신하고 watchdog 진행도를 초기화한다.
- Enemy 디버그 문자열에는 Reform 진행 여부와 감지 횟수, Player 슬롯 집계에는 Formation revision을 표시한다.
- 디버그 표시를 꺼도 Reform 판단은 계속 동작하도록 슬롯 컴포넌트 Tick 책임을 분리했다.

## 기본값

| 항목 | 값 | 의미 |
| --- | ---: | --- |
| `ReformTriggerDistance` | 500 cm | 이 거리 이상 플레이어가 이동하면 포위 재편성 |
| `SlotRepathDistance` | 50 cm | 작은 이동에서 기존 슬롯 목적지 재요청 |
| `StuckTimeout` | 2초 | 개별 적이 슬롯에 도달하지 못할 때 예약 복구 |

## 빌드 검증

- Unreal Header Tool 성공
- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공

## 사용자 PIE 검증

1. Enemy 8~12마리를 배치하고 정지한 플레이어를 포위시킨다.
2. 공격한 Enemy가 회복 후에도 같은 빨간 Attack Slot을 유지하는지 확인한다.
3. 플레이어를 500 cm 미만으로 움직여 Formation revision이 증가하지 않는지 확인한다.
4. 포위를 뚫고 500 cm 이상 이동해 Player 위 `Reform` 숫자가 증가하는지 확인한다.
5. Reform 이후 전열은 전열, 후열은 후열을 유지하면서 가까운 새 슬롯으로 이동하는지 확인한다.
6. 경직·사망·교착 Enemy의 슬롯은 여전히 즉시 반납되는지 확인한다.

## 후속 작업

- Wait Slot 대기 시간과 각도 구역을 고려한 승격 우선순위
- 일정 공격 횟수 또는 교전 시간 이후에만 발생하는 선택적 전열 교대
- Wait Ring에서 텔레그래프 후 진입하는 돌진 공격 권한
- 시체 최소 유지 시간, Corpse Budget, Free Reserve를 포함한 Enemy Pool
