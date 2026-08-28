# Workflow

1. 현행 Enemy 사망·Health·Controller·Slot 반납과 스폰 지점을 고정한다.
2. Pool Manager의 상태 모델, 수량 계약, Spawn Point와 교체 요청 Queue를 구현한다.
3. Enemy에 Pool 소유권, inactive 복제 상태, storage·reactivation API를 구현한다.
4. `Die()`를 Pool 관리와 비관리 흐름으로 분기하고 Manager에 시체를 등록한다.
5. Free 우선, oldest eligible Corpse fallback, 한 Tick 한 명 재활성화 규칙을 연결한다.
6. Health·Gameplay Effect·Movement·Collision·Montage·Controller 초기화를 대조한다.
7. Manager와 Enemy 디버그 수치·로그를 추가한다.
8. UHT 및 Editor Development 빌드를 실행해 API·복제 오류를 수정한다.
9. SSOT와 작업 기록에 현행 규칙, 에디터 배치와 PIE 시나리오를 정리한다.
10. 실제 PIE 증거와 빌드 증거를 분리하고 `re0-memo`에서 유지/재작업을 결정한다.

