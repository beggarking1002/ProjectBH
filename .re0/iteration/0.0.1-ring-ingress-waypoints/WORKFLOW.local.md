# Workflow

1. 현재 `GetMoveGoalForReservedSlot()`과 Controller의 이동 재요청 조건을 읽고 스테이지 반복 위험을 파악한다.
2. 슬롯 종류별 정렬 링, 진입 링, 각도 수용 오차를 명시한다.
3. 각도 정렬 → 방사형 진입을 반환하는 분산형 Ring Ingress Waypoint를 구현한다.
4. Debug 텍스트에 Direct/Orbit 이외의 Ring Align/Ingress 단계를 구분할 수 있게 한다.
5. 정적 코드 검토로 플레이어 이동, 슬롯 재배치, 승격 시 단계가 초기화되는지 확인한다.
6. `ProjectBHEditor Win64 Development` 빌드를 수행한다.
7. 코드 QA와 작업 기록을 작성하고, 사용자가 UE Editor에서 실행할 4/8/12마리 검증 절차를 전달한다.
8. `re0-memo`로 이번 사이클의 학습, 반패턴, 다음 게이트를 기록한다.
