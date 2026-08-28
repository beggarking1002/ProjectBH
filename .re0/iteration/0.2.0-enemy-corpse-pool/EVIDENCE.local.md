# Evidence

## Gate 1 — Capacity와 Active Limit

- [x] 고정 Pool prewarm — `InitializePool()`이 `PoolCapacity`만큼 deferred spawn
- [x] Spawn 성공 수 기반 유효 Capacity — 런타임 한도 계산이 `PoolEnemies.Num()` 사용
- [x] Alive 수 Active Limit 제한 — 초기 활성화와 보충 요청 모두 effective limit 사용
- [ ] PIE에서 `Alive <= ActiveEnemyLimit` 장기 확인

## Gate 2 — Free 우선

- [x] Free 존재 시 시체 보존 — `ProcessOneReadyRespawn()`이 `AcquireFreeEnemy()` 우선 호출
- [x] Free Enemy만 교체 스폰에 사용
- [ ] PIE에서 Free 감소 중 Corpse 수 불변 확인

## Gate 3 — 시체 단일 회수

- [x] Free 고갈 조건
- [x] 가장 오래된 eligible Corpse 선택 — Death Sequence 최소값
- [x] Manager Tick당 최대 한 명 회수·재활성화 — Tick당 `ProcessOneReadyRespawn()` 1회
- [ ] PIE에서 가장 오래된 시체 한 구만 사라지는지 확인

## Gate 4 — 시체 지속

- [x] Pool Enemy의 LifeSpan 제거 — `Die()` Pool 분기의 `SetLifeSpan(0)`
- [x] Corpse 상태 가시성 유지 — Corpse는 `bIsPoolInWorld=true` 유지
- [x] 비Pool Enemy 기존 LifeSpan 보존 — 기존 `DeadActorLifeSpan` 분기 유지
- [ ] PIE에서 Pool 시체가 5초 뒤에도 남는지 확인

## Gate 5 — 재활성화 초기화

- [x] Health/Effect 코드 초기화
- [x] Combat State/Attack Context/Timer 코드 초기화
- [x] Collision/Movement 코드 초기화
- [x] 새 AI Controller 생성
- [x] Spawn Transform 순간이동
- [ ] PIE에서 재사용 Enemy 한 생애 전체 확인

## Gate 6 — 이전 생애 격리

- [x] Slot·Queue 사망 반납 — 기존 `Die()` 흐름 보존
- [x] Montage 중단 — 활성화 Multicast에서 `StopAnimMontage()`
- [x] 이전 Gameplay Effect 제거
- [ ] PIE에서 이전 Target·Slot·Effect 잔존 여부 확인

## Gate 7 — Network 표현

- [x] Pool in-world 상태와 Manager 참조 복제
- [x] inactive Client Hidden·Tick·Collision 코드 적용
- [x] activation presentation reset Multicast
- [ ] Listen Server Client 검증
- [ ] Late Join Client의 사망 마지막 Pose 검증

## Gate 8 — 설정 안전성

- [x] Enemy Class 누락 로그
- [x] Spawn Point 누락 fallback grid
- [x] Spawn 실패 로그
- [x] Free Storage를 Kill Z/World Bounds 밖에 두지 않음

## Gate 9 — 빌드

- [x] UHT
- [x] C++ compile — `BHEnemy.cpp`, `BHEnemyPoolManager.cpp`
- [x] Editor Development link — `UnrealEditor-ProjectBH.dll`, Result `Succeeded`

## Gate 10 — 문서

- [x] README/CLAUDE 존재 확인 — 작업 기록 README만 존재, 프로젝트 지침용 CLAUDE 없음
- [x] [[몬스터 이동 시스템 규칙]] 갱신
- [x] 날짜별 작업 기록

## 증거 경계

- 빌드 성공은 UHT·컴파일·링크 호환성만 증명한다.
- 런타임 수량은 Manager 디버그 문자열만 믿지 않고 월드의 Alive·Corpse·Hidden 개체와 함께 대조한다.
- 시체 선택 순서, 5초 최소 표시시간, AI 재추적과 Client 표현은 PIE 증거가 생기기 전까지 미검증으로 남긴다.

## Sip 냉독

- 맥락 없는 별도 검토자가 목적·세 상태·Free 우선·시체 단일 회수·미검증 범위를 정확히 재구성했다.
- `Pending Ring` 오기, Manager 사용 기준, Spawn Point 순환/fallback, Tick 지연과 동일 Actor 식별 절차를 보강했다.
