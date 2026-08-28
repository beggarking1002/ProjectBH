# 2026-08-28 - Detour Crowd 조향 설정 강화

## 목적

UE 기본값에서 비활성화된 Crowd Separation과 낮은 Avoidance 품질을 명시적으로 설정해 Enemy끼리 밀착하거나 서로 끼는 현상을 완화한다. Gate 없이 기존 Attack/Wait/Holding 목적지 구조를 유지한다.

## Codex 변경 사항

### Blueprint 노출 설정

`ABHCrowdEnemyAIController`에 `AI|Crowd` 카테고리 설정을 추가했다.

| 속성 | 기본값 | 역할 |
| --- | ---: | --- |
| Enable Crowd Obstacle Avoidance | On | 다른 Agent와 장애물 회피 |
| Enable Crowd Separation | On | Agent 사이 간격 확보 |
| Crowd Separation Weight | 2.0 | 간격 확보 힘 |
| Crowd Avoidance Quality | High | Detour 회피 샘플 품질 |
| Enable Crowd Anticipate Turns | On | 경로 굴곡 사전 대응 |
| Crowd Collision Query Range | 500 cm | 주변 Agent/장애물 탐색 범위 |
| Crowd Avoidance Range Multiplier | 1.2 | 회피 샘플 탐색 범위 배율 |

UE의 `ECrowdAvoidanceQuality::Type`은 Blueprint UENUM이 아니므로 프로젝트용 `EBHCrowdAvoidanceQuality`를 추가하고 Low, Medium, Good, High를 UE 타입으로 변환한다.

### 런타임 적용

- 서버 `OnPossess` 직후 `GetPathFollowingComponent()`를 `UCrowdFollowingComponent`로 캐스팅한다.
- 각 설정을 Crowd Following Component의 공식 Setter로 적용한다.
- Detour Crowd Component가 아닌 경우 Warning 로그를 남긴다.
- 설정 적용 로그는 Verbose로 남겨 일반 플레이 로그를 과도하게 채우지 않는다.

## 사용자 에디터 작업

C++ 기본값은 별도 작업 없이 모든 `ABHCrowdEnemyAIController`에 적용된다.

에디터에서 값을 조절하려면 다음 작업을 수행한다.

1. Content Browser에서 `C++ Classes/ProjectBH/AI/BHCrowdEnemyAIController`를 찾는다.
2. 우클릭 후 Blueprint Class Based on BHCrowdEnemyAIController를 생성한다.
3. 이름은 예를 들어 `BP_CrowdEnemyAIController`로 둔다.
4. Class Defaults의 `AI|Crowd` 카테고리에서 값을 조절한다.
5. `BP_Enemy_Base`의 Class Defaults에서 `AI Controller Class`를 새 Blueprint Controller로 지정한다.

튜닝이 필요하지 않으면 Blueprint Controller를 만들 필요가 없다.

`Project Settings → Engine → Crowd Manager`에서는 `Max Agents`를 동시 활성 Enemy보다 여유 있게 유지하고 `Max Agent Radius`가 가장 큰 Enemy Capsule 반지름 이상인지 확인한다.

Enemy Character Movement의 `Use RVOAvoidance`는 계속 끈다. Detour Crowd와 RVO를 동시에 사용하지 않는다.

## PIE 검증

1. 넓은 평지에서 4마리, 8마리, 12마리를 순서대로 비교한다.
2. 이동 중 Capsule이 맞닿기 전에 좌우로 간격을 확보하는지 확인한다.
3. Attack/Wait Slot 도착 후 지속적인 미세 진동이 생기지 않는지 확인한다.
4. 좁은 통로에서 마주보는 Agent가 이전보다 빨리 한 방향으로 정렬되는지 확인한다.
5. 12마리에서 프레임 비용이 허용 범위인지 확인한다.

미세 진동이 증가하면 다음 순서로 완화한다.

1. Crowd Separation Weight를 2.0에서 1.5로 낮춘다.
2. 그래도 흔들리면 1.0을 비교한다.
3. 성능 부담이 크면 Avoidance Quality를 High에서 Good으로 낮춘다.
4. 너무 일찍 크게 우회하면 Avoidance Range Multiplier를 1.2에서 1.0으로 낮춘다.

## 검증

- `git diff --check` 통과
- Unreal Header Tool 성공
- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 및 DLL 링크 성공

## 관련 파일

- `Source/ProjectBH/AI/BHCrowdEnemyAIController.h`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`
- UE 5.7 `Navigation/CrowdFollowingComponent.h/.cpp`
- `ProjectBH/기획/군중 몬스터 이동 및 전투 자리 배정 설계 초안.md`

## 남은 작업 / 다음 단계

- PIE에서 간격 확보와 슬롯 도착 진동을 함께 관찰한다.
- 기본값이 너무 공격적이면 Separation Weight를 먼저 낮춘다.
- Crowd Manager의 Max Agents와 Max Agent Radius를 프로젝트 활성 Enemy 상한에 맞춘다.
