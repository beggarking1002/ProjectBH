# Workflow

1. 현행 Controller, Slot Component, Enemy 상태와 호출 관계를 고정한다.
2. Attack 안전 거리와 Queue 보존 API를 먼저 구현해 생명주기 계약을 정리한다.
3. Reform의 전투 잠금 고정 규칙을 구현한다.
4. 도달 가능성·고정 시간·거리 우위를 가진 타깃 선택을 구현한다.
5. Pending 동심 Ring 위치와 Controller 이동을 구현한다.
6. Nav Path Point 기반 혼잡 패널티를 Admission 세 경로에 연결한다.
7. UHT와 Editor Development 빌드를 실행하고 컴파일 오류를 수정한다.
8. 코드 대조 및 디버그 문자열을 검토하고 기준 문서·작업 기록을 갱신한다.
9. 실제 PIE에서 사용자가 확인할 시나리오를 체크리스트로 남긴다.
10. `re0-memo`로 이번 사이클의 교훈·안티패턴·다음 게이트를 기록하고 유지/재작업을 결정한다.

