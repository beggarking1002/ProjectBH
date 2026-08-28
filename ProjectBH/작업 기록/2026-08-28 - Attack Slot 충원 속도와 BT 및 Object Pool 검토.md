# 2026-08-28 - Attack Slot 충원 속도와 BT 및 Object Pool 검토

## 결론

- Attack 승격은 한 Tick에 한 명씩 처리되지 않는다. 유효 후보가 있으면 Slot Component의 0.1초 Tick 안에서 빈 Attack Slot을 반복해서 채운다.
- `A` 집계 자체가 천천히 증가한다면 가장 유력한 코드상 원인은 Wait Enemy가 자신의 Wait Slot에서 `PromotionArrivalRadius = 60 cm` 안에 먼저 도착해야 Attack 승격 후보가 되는 규칙이다. 실제 병목 확정에는 PIE 시간 측정이 필요하다.
- 현재 수준의 추적·자리 배정·공격 반복에는 Behavior Tree가 필수는 아니다. 행동 종류가 늘어날 때 고수준 의사결정 계층으로 도입하고 Slot·Queue 권한은 현행 C++ 중앙 시스템에 유지하는 것이 적합하다.
- Object Pool은 아직 구현되지 않았다. 현재 Enemy는 사망 시 Slot 반납, 이동·Collision 비활성화와 Controller 분리 후 기본 5초 뒤 `SetLifeSpan()`으로 제거된다.

## Attack Slot이 늦게 차는 이유

### Initial 단계

- 신규 Enemy 등록이 끝난 뒤 `InitialFormationSettleTime = 0.5초`를 기다린다.
- 정착 시간이 끝나면 경로·혼잡 점수로 최대 4명의 Attack 예약을 같은 편성 처리에서 확정한다.
- 머리 위 `A`는 공격 중인 수가 아니라 **점유된 Attack 예약 수**다. 따라서 집계가 빠르게 `A:4`가 되었는데 Enemy가 아직 Attack Ring에 없다면, 예약이 느린 것이 아니라 실제 이동 시간이 긴 것이다.

### Runtime 단계

- 빈 Attack Slot은 Wait 예약자만 승격해 채운다.
- Wait 예약자는 먼저 자기 Wait Slot에서 60 cm 이내에 도착해야 한다.
- 후보가 생기면 `while (PromoteBestWaitReservationToAttack())`가 같은 0.1초 Tick 안에서 가능한 수만큼 승격한다.
- 그러므로 `A` 값 자체가 천천히 증가한다면 순차 처리 때문이 아니라 Wait 도착 조건 때문에 후보가 늦게 만들어지는 것이다.

## 권장 개선 방향

빈 Attack Slot을 빠르게 채울 때는 `Wait En-route Fast Track`을 추가하는 방향을 권장한다.

1. 정상 상황에서는 기존처럼 Wait Slot 도착 후 승격한다.
2. Attack Slot이 일정 시간 비어 있으면 아직 Wait Slot로 이동 중인 Wait 예약자도 후보로 허용한다.
3. 후보는 현행 Nav 경로와 혼잡 점수로 고른다.
4. Holding과 Pending이 Wait를 건너뛰어 바로 Attack으로 가는 것은 허용하지 않는다.
5. 승격된 Wait Enemy는 기존 Wait 경로를 버리고 Attack Slot로 즉시 재경로한다.

첫 검증 후보값은 `AttackVacancyFastTrackDelay = 0.25~0.5초`다. 아직 PIE로 검증한 값은 아니다. 단순히 `PromotionArrivalRadius`를 크게 올리는 방법도 있지만, 정상 승격 규칙까지 느슨해져 의도를 읽기 어렵기 때문에 명시적인 Fast Track 규칙이 포트폴리오 설명에도 더 유리하다.

## Behavior Tree 판단

현행 구조는 역할이 분명하다.

- AI Controller: 타깃·이동·교착 감시
- Slot Component: 중앙 예약·Queue·승격
- Enemy: 공격·회복·경직·사망 상태

추적과 근접 공격만 있는 현재 단계에서는 이 C++ 상태 구조가 충분하다. Behavior Tree를 지금 전면 도입하면 같은 상태를 C++과 BT 양쪽에서 관리할 위험이 있다.

다음 행동이 추가될 시점에는 BT 도입 가치가 커진다.

- 순찰·수색·귀환
- 원거리와 근거리 전환
- 돌진·특수 공격 선택
- 엄폐·도주·지원
- 벽면 Traversal 경로 선택

이때도 BT는 고수준 행동을 선택하고, Slot 요청·Queue Sequence·Admission·교착 복구는 현행 C++ 중앙 시스템을 호출하는 구조로 둔다.

구체적인 도입 기준은 “행동 개수” 자체가 아니라, 상황에 따라 서로 배타적인 고수준 행동을 자주 전환하고 그 선택 규칙을 C++ 재컴파일 없이 편집할 필요가 생겼는지다. 현재처럼 `추적 → Slot 이동 → 공격 → 회복` 한 축이면 전환 이득이 작다.

## Object Pool 현황

현재 미구현이다.

현행 사망 흐름은 다음과 같다.

1. `Dead` 전환
2. Slot과 Queue 반납
3. 이동 비활성화
4. Death Montage 재생
5. 기본 0.2초 뒤 Capsule Collision 비활성화
6. Controller 분리
7. 기본 5초 뒤 Actor 제거

Pool Capacity 40, Active Enemy Limit 12, Corpse Budget 24, Free Reserve 4는 [[몬스터 이동 시스템 규칙]]에 적힌 후속 설계값일 뿐 런타임 구현값이 아니다.

## 다음 결정

Attack Slot 충원 속도를 실제로 수정하기 전에 다음을 디버그로 구분한다.

- `A:4`가 빠르게 표시되지만 몬스터가 늦게 도착: 이동·Ring 진입 경로 문제
- `A`가 0→1→2처럼 천천히 증가: Wait 도착 조건에 의한 승격 지연

두 번째 경우라면 `Wait En-route Fast Track`을 구현한다.

측정할 시점은 `Attack Slot이 빈 순간 → Wait가 60 cm 안에 들어온 순간 → Attack 예약으로 승격된 순간 → Attack Ring 도착 순간`이다. 첫 구간이 길 때만 Fast Track이 직접적인 해결책이다.

## 관련 문서

- [[몬스터 이동 시스템 규칙]]
- [[2026-08-28 - 몬스터 이동 계약 6개 항목 강화]]
