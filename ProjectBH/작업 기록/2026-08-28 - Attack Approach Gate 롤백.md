# 2026-08-28 - Attack Approach Gate 롤백

## 목적

Attack Approach Gate가 Wait Ring 주변 이동을 한 지점에 집중시켜 Enemy끼리 끼는 현상을 악화했으므로 Gate 기능을 제거한다.

## Codex 변경 사항

다음 Gate 전용 요소를 제거했다.

- `EBHCombatApproachStage`
- `AttackApproachGateRadius`
- `AttackApproachAlignmentAngle`
- `ApproachGate`, `OrbitToGate`, `Ingress`, `AtSlot` 단계 이동
- Gate 경유 Admission 비용 계산
- Gate 위치와 Ingress 선 디버그 표시
- AI 디버그의 단계별 Route 문자열

다음 기존 기능은 유지했다.

- Attack Slot 4개, Wait Slot 8개, Holding Slot 16개
- Wait Ring Angle Offset 22.5도
- 초기 Nav 경로 거리 기반 Attack Admission
- Wait 도착 후 Nav 경로 거리 기반 런타임 승격
- Attack 교착 시 Wait 후보와 중앙 교대
- Combat Core를 가로지를 때 사용하는 기존 Wait/Holding Orbit 우회
- 디버그 Route의 `Direct/Orbit` 표시

## Crowd 강화 설정 위치

Crowd 강화값은 현재 프로젝트에 아직 적용되어 있지 않다.

### Enemy별 이동 설정

적용 위치는 `ABHCrowdEnemyAIController`의 `OnPossess` 또는 별도 초기화 함수다. `GetPathFollowingComponent()`를 `UCrowdFollowingComponent`로 캐스팅한 뒤 다음 API를 호출한다.

- `SetCrowdSeparation`
- `SetCrowdSeparationWeight`
- `SetCrowdAvoidanceQuality`
- `SetCrowdAnticipateTurns`
- `SetCrowdCollisionQueryRange`
- `SetCrowdAvoidanceRangeMultiplier`

이는 각 Enemy Agent의 조향 품질과 간격 확보 방식이다. C++ 기본값을 추가하고 `EditDefaultsOnly` 속성으로 노출하면 AI Controller Blueprint에서 조절할 수 있다.

### 프로젝트 전역 Crowd Manager

Unreal Editor의 `Project Settings → Engine → Crowd Manager`는 다음과 같은 전역 수용량과 Manager 한계를 담당한다.

- Max Agents
- Max Agent Radius
- NavMesh Check Interval
- Path Optimization Interval

Separation 활성화와 Enemy별 Avoidance Quality는 이 화면이 아니라 `UCrowdFollowingComponent` 설정으로 처리한다.

## 사용자 에디터 작업

- 별도 Blueprint 연결 작업은 없다.
- 에디터를 다시 열면 청록색 Gate 구체와 선이 사라져야 한다.
- Enemy 디버그 Route는 다시 `Direct` 또는 `Orbit`로 표시된다.

## 검증

- Gate 관련 심볼이 프로젝트 C++ 소스에서 모두 제거된 것을 확인했다.
- `git diff --check` 통과
- Unreal Header Tool 성공
- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 및 DLL 링크 성공

## 관련 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.h`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`
- `ProjectBH/작업 기록/2026-08-28 - Attack Approach Gate 단계 이동 구현.md`

## 남은 작업 / 다음 단계

- Detour Crowd의 Separation과 Avoidance 품질 명시 설정은 [[2026-08-28 - Detour Crowd 조향 설정 강화]]에서 구현했다.
- Crowd 설정 적용 후 넓은 평지와 좁은 통로에서 4, 8, 12마리를 비교한다.
