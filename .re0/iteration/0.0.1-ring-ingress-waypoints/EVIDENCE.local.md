# Evidence

- [x] Distributed route gate: Attack은 Wait Ring, Wait는 Holding Ring에서 자신의 최종 Slot 각도를 사용하며, Controller Debug가 각 Enemy의 중간 목표를 경로선과 구체로 표시한다.
- [x] Monotonic staging gate: `PreviousRouteStage == Ingress`이고 Combat Core 직선이 안전하면 같은 예약의 최종 Slot을 계속 사용한다. Slot 변경·Reform·반납·교착 교대에서만 단계를 초기화한다.
- [x] No regression code gate: 기존 `DoesSegmentCrossCombatCore()` 우회를 유지했고, Ring 진입이 필요 없으며 직선이 안전한 경우 `Direct`를 반환한다. 런타임 PIE 회귀는 미확인이다.
- [x] NavMesh code gate: 중간 목표는 `ProjectToNavigation()` 성공 시에만 사용하며, 실패를 반환하면 Controller가 예약을 반납하고 이동을 중단한다.
- [x] Build gate: 2026-08-28 UHT, `BHCrowdEnemyAIController.cpp`, `CombatEngagementSlotComponent.cpp`, DLL link 성공. 냉독 QA 수정 후 두 번째 빌드도 `Result: Succeeded`.
- [ ] Real-surface gate: UE Editor 4/8/12/13+마리 PIE는 사용자 실행 필요. 12마리는 Attack+Wait 포화, 13+마리는 Holding 진입을 검증한다. Debug는 `Direct` 흰색, `ApproachRing` 청록, `AlignOnRing` 파랑, `Ingress` 주황으로 준비됨.
- [x] Documentation reflection gate: Obsidian 군중 설계 문서와 작업 기록을 현재형으로 반영했다. 루트 CLAUDE/README는 현재 없고 새 사용자 기능이 아니므로 생성하지 않았다.

## Paperthin QA

- `shower`: 코드의 목적과 4단계 전환을 올바르게 읽었다. 중심 겹침 반지름 오류와 이론 Ring/투영 Slot 반지름 불일치를 발견해 수정했다. 반환값 무시 지적은 전달한 호출부 축약으로 생긴 오판이며 실제 코드는 실패 시 예약을 반납한다.
- `factchk`: Epic 공식 Navigation System/Avoidance 문서와 UE 5.7 로컬 엔진 소스가 NavMesh 경로 탐색과 Detour Crowd 국소 회피의 역할 분리, RVO와의 배타적 사용 설명을 뒷받침한다.
- `ssotize` 읽기 전용 감사: 신규 1도/5 cm 기본값은 C++ 헤더가 단일 원천이다. 과거 Gate 문서의 `Ingress`는 롤백된 음성 사례로 분류되므로 현행 명세와 통합하지 않았다.
- `re0`: 기획 문서의 기존 OrbitRoute 설명을 현재 4단계 분산형 Ring Ingress 규칙으로 교체했다.
