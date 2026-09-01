# 빈 Attack Slot 승격 fallback

## 문제

Attack Slot이 초록색인 채 유지되는데 Wait Enemy가 점유하지 않는 사례가 있었다.

원인은 두 종류였다.

1. Pocket·Corridor의 현재 공간 수용량 밖 인덱스까지 초록색 Slot으로 표시되어, 실제로는 비활성인 자리가 빈자리처럼 보였다.
2. 실제 활성 빈자리도 Wait Enemy가 자기 Wait Slot의 `60 cm` 안에 도착해야만 승격할 수 있어, 외곽 대형이 이동 중이거나 혼잡할 때 오래 남을 수 있었다.

## 구현 규칙

- Attack 초기 배정, 런타임 승격 탐색, 디버그 구체와 `A` 분모는 `GetActiveAttackSlotCount()` 범위만 사용한다.
- 활성 Attack 인덱스는 항상 `0`부터 `활성 수 - 1`까지의 연속 구간이다. 수용량이 바뀌면 기존 예약을 새 활성 범위에 맞게 재조정하며, 공석 타이머는 같은 Tick에서 갱신된 활성 수를 사용한다.
- 비활성 Attack 인덱스는 화면에 그리지 않는다.
- 활성 Attack Slot별 빈 시간은 실제 `DeltaTime`을 누적한다. Initial 단계, 점유 중인 Slot, 현재 활성 범위 밖 Slot은 매 Tick `0초`로 초기화한다. 새로 활성화된 빈 Slot은 `0초`부터 시작한다.
- Combat Core 탈출 중에도 공석 시간은 누적한다. 탈출이 끝났을 때 이미 1초를 넘겼다면 같은 Tick의 정상 승격 다음에 fallback을 바로 검사해 전열을 다시 채운다.
- 정상 Wait→Attack 승격을 항상 먼저 전부 시도한 뒤 fallback을 전부 시도한다. 한 `0.1초` Slot Tick에서 조건을 만족하는 여러 빈자리를 연속으로 채울 수 있다.
- 빈 시간이 `AttackVacancyFallbackDelay = 1.0초` 이상인 Slot만 fallback 대상이 된다.
- fallback은 Wait Slot 도착 반경 조건만 해제한다.
- Attack 재진입 쿨다운, Enemy별 공격 가능 거리, 완전하고 비부분적인 Nav 경로는 정상 승격과 동일하게 요구한다.
- Open·Pocket은 Enemy 현재 위치에서 Attack Slot까지의 완전 경로와 혼잡 비용을 비교한다. Corridor는 같은 Side와 빠른 Queue Sequence를 우선하고, 같은 Side에 후보가 없을 때 반대 Side까지 확장한다. fallback도 이 선정 규칙을 바꾸지 않는다.
- 공격 가능 거리는 Player에서 후보 Attack Slot까지의 2D 거리와 해당 Enemy의 안전 최대 Attack Slot 거리를 비교한다.
- Combat Core 탈출 중에는 전열을 닫지 않도록 정상 승격과 fallback을 모두 중단한다.

## 디버그 표시

- 초록 Attack 구체: 활성 빈자리, fallback 대기 중
- 주황 Attack 구체: 1초 이상 빈 활성 자리, fallback 검사 가능
- 빨강 Attack 구체: 점유됨
- `VacA`: 활성 빈자리 중 가장 긴 누적 시간
- `Promote:Ready`: 승격을 검사할 수 있음
- `Promote:CoreEscape`: Combat Core 탈출 때문에 승격이 일시 중단됨

주황 구체가 계속 남는다면 Wait 도착만의 문제가 아니다. 자격 있는 Wait Enemy 부재, 재진입 쿨다운, 공격 가능 거리, 완전 경로 실패, 또는 `CoreEscape` 억제를 확인한다.

## 검증

1. Open 평지에서 8마리 이상을 두고 Attack 점유자가 사망·경직·교착 반납하도록 만든다.
2. 쿨다운·사거리·경로 조건을 통과하고 자기 Wait Slot `60 cm` 안에 있는 후보를 만든다. 빈자리가 생기면 `VacA < 1.0`에서 실제 Enemy의 예약이 Wait에서 Attack으로 바뀌는지 본다.
3. 쿨다운·사거리·경로 조건은 통과하지만 자기 Wait Slot `60 cm` 밖에서 이동 중인 후보를 만든다. 녹화 시간 기준 약 1초 뒤 실제 Enemy 예약이 fallback으로 바뀌는지 확인한다. 초록→주황과 `VacA`는 같은 타이머에서 나온 보조 표시이므로 이것만으로 합격 처리하지 않는다. `0.1초` Tick 양자화 때문에 전환은 최대 한 Tick 늦게 보일 수 있다.
4. Pocket·Corridor에서 수용량을 변화시킨다. `A` 분모와 구체 수뿐 아니라 각 Enemy 머리 위 Attack 인덱스도 확인해 `활성 수 이상` 인덱스를 점유한 Enemy가 없는지 검증한다.
5. `Promote:CoreEscape` 동안 승격이 멈추고 탈출 종료 뒤 다시 채워지는지 본다.
6. 주황색이 계속 남으면 영상만으로 합격 처리하지 않고 `VacA`, `Promote`, 해당 Enemy 쿨다운·경로·사거리 상태를 함께 기록한다.

## 빌드

- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공
- 실제 군중 승격 연출은 PIE 검증이 필요하다.
