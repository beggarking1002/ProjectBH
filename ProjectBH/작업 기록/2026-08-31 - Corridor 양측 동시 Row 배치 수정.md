# Corridor 양측 동시 Row 배치 수정

## 이해한 작업 범위

Corridor의 Wait·Holding·Pending 대형은 Player 기준 축 양쪽에 동시에 생성되어야 한다. 가까운 한쪽 Row만으로 계층의 전체 Slot 수를 채울 수 있어도 반대쪽을 추가 탐색해 가능한 한 절반씩 배치한다. 한쪽에 실제 NavMesh 수용량이 부족할 때만 부족분을 유효한 반대쪽으로 넘긴다.

`Side 0`은 확정된 Corridor 축의 Rear 방향이고 `Side 1`은 그 반대 방향이다. 이는 한 Row 내부의 횡방향 Lane 좌우가 아니라 Player에서 Corridor 축을 따라 갈라지는 앞뒤 두 영역이다.

## 원인

기존 `BuildCorridorLayerLayout`은 Player에서 가까운 Row부터 양 Side를 검사하지만, `OutLayout.Num() == RequiredSlotCount`가 되는 순간 전체 탐색을 끝냈다. 한쪽의 초기 Row만 유효한 장면에서는 그 Side가 전체 Slot을 먼저 채웠고, 더 먼 반대쪽 Row의 유효 공간은 검사하지 않았다. Player가 조금 움직여 먼저 유효해지는 Side가 바뀌면 전체 대형도 반대편으로 뒤집혔다.

## 수정 계약

1. Wait 8개는 양쪽 수용량이 충분하면 `4:4`로 배치한다.
2. Holding 16개는 양쪽 수용량이 충분하면 `8:8`로 배치한다.
3. Pending도 요청된 수를 양쪽에 가능한 한 균등하게 배치한다.
4. 한쪽의 가까운 Row만으로 총 필요 수가 충족돼도 처음 충족한 Row 다음의 기본 6개 Row까지 반대쪽 유효 공간을 찾는다.
5. 양쪽 선호 수가 모두 확보되면 즉시 탐색을 끝낸다.
6. 한쪽 수용량이 선호 수보다 적으면 실제 확보된 수만 사용하고 나머지를 다른 쪽에 Spillover한다.
7. 각 Side 안에서는 기존처럼 가까운 Row와 중앙 Lane을 우선한다.
8. Mouth 전환 중에도 확정 Corridor 축을 기준으로 이 균형 Row 갱신을 허용한다.

## 비포함 범위

- Attack Slot 360도 후보 선택 규칙
- Corridor/Open/Pocket 판정 임계값
- Enemy별 Side 선호와 Combat Core 우회 점수
- Detour Crowd 설정

## 구현 결과

- `BuildCorridorLayerLayout`이 Side 0과 Side 1 후보를 별도 배열에 수집한다.
- 선호 할당량은 `ceil(필요 수 / 2)`와 `floor(필요 수 / 2)`다.
- 한쪽이 전체 필요 수를 먼저 확보해도 최대 6개 Row를 더 검사한다.
- 양쪽 선호 할당량을 확보하면 즉시 검색을 종료한다.
- 수용량 부족분은 남은 후보가 더 많은 Side에 넘긴다.
- 확정 Slot 배열은 Side를 교대로 추가하며, 각 Side 내부 순서는 가까운 Row·중앙 Lane 우선이다.
- 디버그 공간 문자열에 생성 Slot 수를 계층별로 표시하는 `RowSides W:A/B H:C/D`를 추가했다.
- 직전 Mouth 수정에서 임시로 추가한 Attack·Row 동결은 제거했다. Attack 후보 선택 규칙 자체는 변경하지 않았고 Mouth 증거 유지와 축 안정화만 남겼다.
- 양쪽 후보 합계가 전체 필요 수보다 적으면 새 Layout을 확정하지 않고 마지막 확정 Layout을 유지한다.

## 정적 할당 검증

| 필요 수 | 후보 Side 0:1 | 결과 Side 0:1 |
| ---: | ---: | ---: |
| 8 | `8:8` | `4:4` |
| 8 | `8:0` | `8:0` |
| 8 | `8:1` | `7:1` |
| 8 | `2:10` | `2:6` |
| 5 | `5:5` | `3:2` |
| 5 | `1:10` | `1:4` |
| 16 | `20:20` | `8:8` |
| 16 | `16:4` | `12:4` |
| 8 | `2:2` | 새 Layout 확정 안 함 |

## 검증 상태

- `git diff --check`: 통과
- UE 5.7 `ProjectBHEditor Win64 Development`: 빌드 성공
- PIE: 사용자 확인 필요

## PIE 확인 절차

1. `bh.Debug.Slots 1`을 켜고 첨부 화면의 같은 Corridor 위치로 이동한다.
2. Wait 8개와 Holding 16개의 양쪽 공간이 모두 유효하면 `RowSides W:4/4 H:8/8`인지 확인한다.
3. Player가 좌우로 조금 움직여도 대형 전체가 한 Side에서 반대 Side로 넘어가지 않는지 확인한다.
4. 한쪽 Row가 가까이에서 유효하지 않아도 6개 Row 안쪽에 유효 공간이 있으면 그 Side에 Slot이 생기는지 확인한다.
5. 한쪽을 지형으로 완전히 막았을 때는 가짜 Slot을 만들지 않고 유효한 쪽으로 Spillover하는지 확인한다.
6. 통로 중앙에서 Wait `4:4`, Holding `8:8`이 유지되고 승격·사망·재배치가 기존처럼 동작하는지 회귀 확인한다.
7. Pending을 확인할 때는 Attack 5 + Wait 8 + Holding 16을 넘는 Enemy를 등록한다. 각 Pending Enemy의 디버그 Side와 목적지가 양쪽에 가능한 한 균등한지 확인한다. 홀수 Pending 수의 한 자리 차이는 Side 0이 더 많을 수 있다.

`RowSides`는 Layout 배열을 코드가 직접 집계한 자기 보고 값이므로 이것만으로 합격 처리하지 않는다. Slot 구체가 Player 기준 양쪽의 서로 다른 월드 좌표에 실제로 그려지고, 예약 Enemy가 양쪽 목적지로 이동하는 장면을 함께 확인한다. 한쪽을 막는 검증에서는 반대쪽이 전체 필요 수를 수용할 수 있는 NavMesh 폭과 길이를 먼저 확보하고, 실제 이동 경로가 막힌 상태도 함께 확인해 Spillover가 단순 디버그 숫자 변경이 아닌지 판정한다.
