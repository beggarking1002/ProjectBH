# Combat Core 및 Wait Ring 경유 포위 접근 구현

## 요청 이해

Enemy가 반대편 전투 슬롯으로 가는 직선 경로에서 Player Capsule에 막히는 문제를 해결한다. Player를 NavMesh 동적 장애물로 만들지 않고, 전방 슬롯 우선 예약과 Wait Ring 경유 이동을 하나의 접근 규칙으로 구현한다. 12개 슬롯을 초과한 Enemy의 Holding Ring과 중앙 대기열은 다음 구현 묶음으로 남긴다.

## 구현 결과

- Player 중심에 기본 반경 100 cm의 논리적 `Combat Core`를 추가했다.
- Enemy 위치에서 후보 슬롯까지의 2D 선분이 Combat Core를 통과하는지 검사한다.
- 빈 슬롯 중 Combat Core를 통과하지 않는 직접 접근 슬롯을 먼저 예약하고, 같은 분류에서는 가장 가까운 슬롯을 선택한다.
- 직접 접근 슬롯이 없으면 반대편 슬롯도 예약할 수 있지만 바로 MoveTo하지 않는다.
- Enemy를 현재 방사 방향의 Wait Ring 위치로 먼저 이동시킨다.
- Wait Ring에 도달하면 목표 슬롯 방향으로 최대 45도씩 선회하는 NavMesh 경유점을 생성한다.
- 최종 슬롯까지의 직선이 Combat Core를 통과하지 않게 되면 경유를 끝내고 슬롯으로 직접 진입한다.
- 각 경유점은 NavMesh 위로 투영하며 투영 실패 시 예약 오류 복구 경로로 전환한다.
- 이동 완료 시 watchdog 진행도를 초기화해 다음 경유점의 거리 변화가 이전 단계와 섞이지 않게 했다.
- Enemy 디버그 문자열에 `Route:Direct`와 `Route:Orbit`를 표시한다.
- Player 슬롯 디버그에는 Combat Core를 주황색 원으로 표시한다.

## 기본값

| 항목 | 값 | 의미 |
| --- | ---: | --- |
| `CombatCoreRadius` | 100 cm | Enemy 직접 이동 경로가 통과할 수 없는 Player 중심 반경 |
| `WaitRingRadius` | 300 cm | 반대편 슬롯 접근 시 사용하는 선회 반경 |
| `OrbitWaypointAngleStep` | 45도 | 경유점 하나당 최대 선회 각도 |
| `OrbitRingAcceptanceRadius` | 35 cm | Wait Ring에 정렬됐다고 판단하는 반경 오차 |
| `SlotRepathDistance` | 50 cm | Player 이동으로 목표가 바뀌었을 때 재경로 기준 |

`CombatCoreRadius`는 런타임에 Attack Ring 반경보다 작도록 제한해 최종 공격 슬롯 자체가 접근 금지 영역 안에 갇히지 않게 한다.

## 빌드 검증

- Unreal Header Tool 성공
- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공

## 사용자 PIE 검증

1. Enemy 4~8마리를 Player 한쪽 방향에 모아 배치한다.
2. 가까운 전방 슬롯을 얻은 Enemy가 `Route:Direct`로 진입하는지 확인한다.
3. 반대편 슬롯을 얻은 Enemy가 `Route:Orbit`로 바뀌고 주황색 Combat Core 밖으로 선회하는지 확인한다.
4. 선회가 끝나면 `Route:Direct`로 돌아와 최종 슬롯에 도달하는지 확인한다.
5. Player가 경로 중간에서 움직여도 Enemy가 Capsule 앞에서 2초 이상 멈추거나 `Stalled`를 반복하지 않는지 확인한다.
6. 좁은 통로와 벽이 있는 장소에서 각 경유점이 초록색 NavMesh 영역 위에 놓이는지 확인한다.

## 현재 제한과 다음 작업

- 복잡한 벽 배치에서 NavMesh가 경유점 사이의 예상과 다른 우회 경로를 선택하는지는 PIE 검증이 필요하다.
- 후속 작업에서 12마리 초과 Enemy용 Holding Ring 16개와 중앙 승격 대기열을 구현했다. [[2026-08-27 - Holding Ring 및 중앙 승격 대기열 구현]]을 참고한다.
- 돌진형 특수 진입은 Holding과 승격 규칙이 안정된 뒤 별도로 구현한다.
