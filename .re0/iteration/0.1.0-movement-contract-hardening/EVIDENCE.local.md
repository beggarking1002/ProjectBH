# Evidence

## Evidence boundary

- 코드 계약: 아래 체크된 항목은 구현 대조로 확인했다.
- 빌드: UE 5.7 `ProjectBHEditor Win64 Development`의 UHT, 컴파일, 링크로 확인했다.
- 실제 군중 행동: Unreal Editor PIE가 필요하므로 아직 통과로 표시하지 않는다. [[2026-08-28 - 몬스터 이동 계약 6개 항목 강화]]의 사용자 PIE 검증 절차가 실제 표면 검증이다.

## Gate 1 — Attack 거리 단일 기준

- [x] `GetMaximumAttackSlotDistance()` 단일 계산 함수
- [x] Initial Admission 사용
- [x] Runtime Wait→Attack 사용
- [x] Stalled Attack 교대 사용
- [x] Controller 직접 예약 사용
- [ ] PIE에서 NavMesh 투영으로 범위를 벗어난 Attack Slot이 제외되고 RangeMismatch 루프가 없는지 확인

## Gate 2 — 공격 잠금 Reform 고정

- [x] Reform 코드가 Attacking Slot을 예약 배열에 그대로 보존
- [x] Reform 코드가 Recovering Slot을 예약 배열에 그대로 보존
- [x] 잠기지 않은 Enemy만 남은 Slot 후보에 재배정
- [ ] PIE에서 이동 중인 플레이어를 따라 Reform되어도 잠긴 Slot 인덱스가 유지되는지 확인

## Gate 3 — 타깃 안정성

- [x] NavMesh 투영과 완전한 비부분 경로가 없는 후보 제외
- [x] `MinimumTargetHoldTime` 적용
- [x] `TargetSwitchDistanceAdvantage` 적용
- [x] 현재 타깃 무효·도달 불가 시 즉시 교체 경로
- [ ] 2인 Listen Server PIE에서 2초/200 cm 히스테리시스와 도달 불가 교체 확인

## Gate 4 — Queue 우선권

- [x] 지정한 일시 실패에서 `ReleaseSlot(..., true)`로 Sequence 보존
- [x] 사망·경직·타깃 이탈에서 Sequence 제거
- [x] Attack 교착 교대가 Queue Entry를 건드리지 않고 Slot만 교환
- [x] 일시 반납 뒤 동일 Slot Component 재연결이 실패 Slot 제외 정보를 지우지 않음
- [ ] PIE의 `Seq` 디버그 값으로 Stalled 전후 동일성과 경직 후 재발급 확인

## Gate 5 — Pending 이동

- [x] 미예약 Queue Sequence 순서로 고유 Pending 인덱스 계산
- [x] 24개 단위 동심 Ring 분산 위치
- [x] Pending 위치 NavMesh 투영
- [x] Slot Type/Index 변경 시 기존 이동 요청과 Route Stage 폐기
- [ ] 32마리 PIE에서 A4/W8/H16/Q4와 네 Pending의 분산 이동·선임 승격 확인

## Gate 6 — 혼잡 Admission

- [x] 완전한 비부분 Nav Path의 Path Point 사용
- [x] 같은 층 Agent당 최대 1회 패널티
- [x] 초기 Admission 적용
- [x] Wait 승격 적용
- [x] 교착 교대 적용
- [ ] PIE 영상에서 패널티 0/200을 비교해 선택 차이가 관측되는지 확인

## Gate 7 — 기존 구조 회귀

- [x] C++ 기본값 Attack 4 / Wait 8 / Holding 16 유지
- [x] Initial settle 뒤 Runtime으로 전환하는 기존 단계 유지
- [x] Runtime 승격 순서 Attack 빈자리 ← Wait ← Holding ← Pending 유지
- [ ] 기존 전투 맵 PIE에서 공격 루프·경직·사망 반납 회귀 확인

## Gate 8 — 빌드

- [x] UE 5.7 UHT 성공
- [x] C++ 컴파일 성공
- [x] `UnrealEditor-ProjectBH.dll` Editor Development 링크 성공
- [x] 문서·주석 정리 후 최종 재빌드 `Result: Succeeded`

## Gate 9 — 문서

- [x] [[몬스터 이동 시스템 규칙]] 갱신
- [x] [[2026-08-28 - 몬스터 이동 계약 6개 항목 강화]] 추가

## Gate 10 — 문서 역할 반영

- [x] 저장소와 Obsidian 루트에 README/CLAUDE가 없음을 확인
- [x] 내부 게임 규칙은 Obsidian 기준 문서와 작업 기록에만 반영

## Cycle decision

**Keep and drive.** 구조를 폐기하거나 `re0-work`할 근거는 없다. 구현과 빌드는 유지하고, 체크되지 않은 PIE 항목을 실제 표면에서 검증한 뒤 발견된 행동 결함만 다음 랩에서 수정한다.

## Paperthin taste test

- `shower`: 작업 기록만 전달한 무맥락 검토자는 구현 범위는 이해했지만 거리 기준점, Queue 범위, Pending 중심과 PIE 재현 조건을 추측해야 했다. 코드에서 확정 가능한 정의와 정확한 에디터 경로를 작업 기록 및 SSOT에 보강했다.
- `mandela`: 빌드 성공을 런타임 성공으로 간주하는 누설을 차단해 PIE Gate를 미완료로 유지했다. 혼잡 A/B는 같은 배치에서 설정별 3회 반복하도록 보강했다.
- `ssotize` 읽기 전용 감사: 현행 수치와 규칙의 기준 문서는 [[몬스터 이동 시스템 규칙]]이며, 작업 기록·Paperthin 문서는 구현 이력과 사이클 증거로 분류했다. 교착 교대에 남아 있던 구형 “최단 경로” 표현 한 건을 현행 혼잡 점수 규칙으로 정정했다. 새 통합이 필요한 충돌은 없다.
- `re0`: 현행 문서를 패치 이력이 아니라 현재 규칙으로 읽히도록 타깃·거리·Queue·Pending·혼잡 정의를 기존 절에 합쳤다.
