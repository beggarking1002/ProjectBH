# Movement Contract Hardening

## Understood as

현재 몬스터 이동 구조를 유지하면서 다음 여섯 문제를 한 사이클에서 구현한다: Attack 거리 기준 통일, 공격·회복 중 Reform 고정, 도달 가능성과 고정 시간을 고려한 타깃 선택, 일시적 이동 실패 시 Queue 우선권 보존, Pending 분산 대기 위치, 실제 경로 주변 Agent를 반영한 Admission 혼잡 점수. Unreal Editor 에셋은 사용자가 담당하므로 C++ 기본값과 Blueprint 노출 지점만 제공하고 `.uasset`은 수정하지 않는다.

## Cycle weight

Full. Controller, Slot Component, Enemy 전투 상태와 기획 문서의 계약을 함께 바꾸며 신규 런타임 행동이 추가된다.

## Model sizing

```text
recommended_tier: frontier
recommended_effort: thorough
rationale: 여러 AI 상태와 예약 생명주기를 동시에 바꾸며 한 규칙의 수정이 교착·공정성·공격 루프에 연쇄 영향을 주므로 아키텍처 수준의 숙고가 필요하다.
move_up_if: 다중 플레이어 PIE에서 재현 불가능한 경쟁 상태나 네트워크 권한 문제가 확인될 때
move_down_if: 각 변경을 독립 함수와 결정론적 자동 테스트로 완전히 격리할 수 있을 때
proof_surface: UE 5.7 Editor Development 빌드, 코드 계약 검사, 디버그 관측값, 사용자 PIE 시나리오
```

## Thesis

중앙 Slot 시스템은 단순히 자리를 나눠 주는 데서 끝나지 않고, 공격 가능 거리·타깃 안정성·예약 우선권·외곽 대기·혼잡 비용까지 실제 이동 계약과 일치시켜야 한다. 복구 코드를 늘리는 대신 배정 단계에서 잘못된 선택을 줄이고, 일시 실패가 전투 우선권을 파괴하지 않도록 한다.

## Scope

1. 모든 Attack Slot Admission은 `AttackStartRange - 실제 Slot 도착 허용오차`를 사용한다.
2. `Attacking`·`Recovering` Enemy는 Attack Ring Reform에서 현재 인덱스를 고정한다.
3. 타깃 후보는 NavMesh 도달 가능해야 하며, 현 타깃은 최소 유지시간과 거리 우위 기준을 통과할 때만 교체한다.
4. 경로·이동의 일시 실패는 Slot만 반납하고 기존 Queue Sequence는 보존한다.
5. Holding을 받지 못한 Pending Enemy는 동심 외곽 Ring의 고유 대기 위치로 이동한다.
6. 초기 Admission, Wait→Attack, 교착 교대는 Nav 경로 길이에 경로 주변 Agent 혼잡 패널티를 더한 점수로 후보를 고른다.

## Out of scope

- Gameplay Ability 전환
- Enemy 전용 Collision Channel
- Object Pool과 벽면 Traversal
- Behavior Tree/EQS 도입
- `.uasset` 수정
- 완전한 동적 Crowd 경로 예측

## Quality gates

1. Attack Slot 허용 거리 계산 함수가 하나이며 모든 Admission 경로가 이를 사용한다.
2. 공격·회복 중인 Attack Enemy의 Slot 인덱스는 Reform에서 변하지 않는다.
3. 도달 불가능한 플레이어는 타깃 후보에서 제외되고, 작은 거리 변화만으로 타깃이 흔들리지 않는다.
4. 일시적 이동 실패 뒤 Queue Sequence가 유지되며 사망·경직·타깃 이탈에서는 제거된다.
5. 29번째 이후 Enemy도 NavMesh 투영된 서로 다른 Pending 위치를 받고 통로에 그대로 정지하지 않는다.
6. Admission 점수는 완전한 Nav 경로 길이와 경로 주변 Agent 패널티를 함께 사용한다.
7. 기존 Attack 4 / Wait 8 / Holding 16과 승격 순서는 유지된다.
8. UHT, C++ 컴파일과 Editor Development 링크가 성공한다.
9. 현행 기준 문서와 날짜별 작업 기록이 새 규칙을 반영한다.
10. 프로젝트 README/CLAUDE 역할을 확인한다. 이 변경은 내부 게임 규칙이므로 README가 아닌 Obsidian 기준 문서에 반영하며, 저장소에 CLAUDE.md가 없으면 새로 만들지 않는다.

