# Movement Contract Hardening Memo

## Preserve

### 예약 시스템이 소유하는 불변식

Attack 가능 거리처럼 모든 배정 경로가 공유해야 하는 값은 호출자가 넘기지 않고 예약 시스템이 계산해야 한다. 이번 사이클에서는 기존에 호출부마다 달라질 수 있던 거리 인자를 `GetMaximumAttackSlotDistance()`로 모았고, Initial·Runtime·교착 교대가 같은 계약을 사용하게 했다.

### 자리와 순번의 분리

Slot 예약은 현재 물리 자원이고 Queue Sequence는 장기 우선권이다. 경로 실패처럼 일시적인 사건에서 둘을 함께 지우면 오래 기다린 Enemy가 계속 뒤로 밀린다. `ReleaseSlot`의 Queue 보존 선택과 실패 사유 분류는 이후 예약 시스템에서도 유지할 계약이다.

### Overflow도 명시적인 이동 상태다

고정 Slot을 받지 못했다는 사실은 AI가 목표를 잃었다는 뜻이 아니다. Pending을 Queue 기반의 가상 위치로 모델링하자 기존 Move·우회·Watchdog·Debug 흐름을 재사용할 수 있었다.

### 중앙 선발은 실제 이동 비용을 봐야 한다

직선거리나 Nav 경로 길이만으로 중앙 진입자를 고르면 정지 군중이 만든 병목을 무시한다. 이번 경로 주변 Agent 패널티는 완전한 예측은 아니지만, 선발 규칙이 실제 이동 문제를 관측하는 최소 기반이다.

## Anti-patterns and gates

### 호출부 인자 표류

- 실패 형태: 같은 Attack 거리 규칙이 호출 경로마다 다른 숫자나 계산식으로 전달된다.
- 증거: 여섯 작업 전에는 예약 API가 최대 거리를 외부 인자로 받았다.
- 다음 게이트: 전투 거리 계약을 바꿀 때 단일 계산 함수의 모든 참조를 검색하고, 새 Admission 경로가 별도 계산을 만들지 않았는지 확인한다.

### 복구가 정체성을 삭제함

- 실패 형태: 이동 복구 과정이 Slot뿐 아니라 Queue 순번과 실패 Slot 제외까지 초기화한다.
- 증거: 일시 반납 후 Slot Component가 `nullptr`인 재획득 경로가 타깃 변경 정리를 실행할 수 있었다.
- 다음 게이트: 모든 Release Reason을 일시 실패와 생명주기 종료로 분류하고, 전자는 Sequence 유지, 후자는 제거를 검증한다.

### 용량 초과를 무행동으로 표현함

- 실패 형태: Holding을 못 받은 Enemy가 현재 위치에 멈춰 통로 병목이 된다.
- 증거: Pending 도입 전에는 29번째 이후 Enemy에게 이동 목적지가 없었다.
- 다음 게이트: 최대 고정 Slot 수를 넘긴 PIE에서 모든 활성 Enemy가 역할과 목적지를 가지며, Pending 목적지가 한 점에 집중되지 않는지 확인한다.

### 빌드를 런타임 증거로 오인함

- 실패 형태: UHT·컴파일·링크 성공을 군중 행동의 성공으로 간주한다.
- 증거: 이번 사이클의 6개 변경은 모두 시간·다수 Agent·NavMesh 배치에 따라 결과가 달라진다.
- 다음 게이트: 빌드는 정적 증거로만 기록하고, 32마리·2인·혼잡 비교 PIE를 별도 실제 표면 증거로 남긴다.

## Discard

- 호출자가 Attack 최대 거리를 정하는 API 형태
- 일시 실패마다 Queue Entry를 재생성하는 복구 방식
- Pending을 단순한 무예약/정지 상태로 취급하는 해석
- Nav 경로 길이만으로 Admission 품질을 판단하는 검증

## Next drive vocabulary

- **Attack safety distance**: `AttackStartRange - SlotAcceptanceRadius`
- **locked reform reservation**: Attacking·Recovering 동안 인덱스를 보존하는 Attack 예약
- **queue-preserving release**: 물리 Slot만 반납하고 Sequence는 유지하는 일시 복구
- **virtual pending slot**: Queue 순번으로 계산하며 예약 배열을 차지하지 않는 외곽 목적지
- **congestion-aware path score**: Nav Path Length와 경로 주변 같은 층 Agent 패널티의 합
- **real-surface proof**: 에디터 PIE에서 다수 Agent와 실제 NavMesh를 사용한 행동 증거

## Decision

**Keep and drive.** 현재 구조는 여섯 계약을 한 위치에서 설명하고 Editor 빌드를 통과했다. 다음 단계는 재작성보다 [[2026-08-28 - 몬스터 이동 계약 6개 항목 강화]]의 PIE 체크리스트를 실행해 런타임 결함을 수집하는 것이다.
