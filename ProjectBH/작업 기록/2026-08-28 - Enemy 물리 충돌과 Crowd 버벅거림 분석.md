# 2026-08-28 - Enemy 물리 충돌과 Crowd 버벅거림 분석

> 상태: 실험 후 효과 없음으로 2026-08-29 롤백. [[2026-08-29 - Enemy 이동 버벅임과 Formation Anchor 진단]] 참고.

## 증상

- Pursuit와 Formation 이동 속도를 높인 뒤 Enemy끼리 맞닿는 구간에서 정지·밀림·방향 전환이 반복된다.
- 특히 이동 Enemy가 이미 Slot에 정지한 Enemy 사이를 통과할 때 버벅거림이 커진다.

## 현재 구현 확인

- `ABHEnemy`는 Capsule Collision Profile을 별도로 지정하지 않아 Character 기본 Pawn 충돌을 사용한다.
- `ABHCrowdEnemyAIController`는 Detour Crowd의 Obstacle Avoidance와 Separation을 모두 활성화한다.
- 현재 Separation Weight는 `2.0`, Collision Query Range는 `500 cm`, Avoidance Range Multiplier는 `1.2`다.
- 빠른 이동 의도와 물리적인 Pawn-Pawn Block이 동시에 적용되어, Character Movement의 충돌 해결과 Crowd 속도 보정이 서로 다른 방향으로 반복 개입할 수 있다.

## 권장 구조

Enemy끼리의 **하드 Capsule Block만 제거**하고, 서로의 공간을 확보하는 책임은 Detour Crowd Separation에 둔다.

1. 전용 Object Channel `EnemyPawn`을 만든다.
2. Enemy Capsule용 Collision Profile을 만든다.
3. Enemy Capsule은 `EnemyPawn`을 Ignore한다.
4. WorldStatic·WorldDynamic은 계속 Block한다.
5. Player는 기존 Pawn을 유지하고, Player와 EnemyPawn은 서로 Block한다.
6. Detour Crowd의 Obstacle Avoidance와 Separation은 유지한다.
7. Enemy Mesh는 이동 충돌을 담당하지 않도록 유지한다.

이 구조에서는 Enemy가 서로 물리적으로 밀어내지는 않지만, Detour Crowd가 Capsule 반경을 기준으로 부드러운 간격을 만들게 한다. 밀도가 지나치게 높거나 목표가 겹치면 순간적인 Mesh 겹침은 발생할 수 있으므로 Slot 간격과 Crowd 설정은 계속 필요하다.

## 반드시 함께 수정할 부분

Player 공격 판정은 현재 `ECC_Pawn`만 Object Query한다. Enemy의 Object Type을 `EnemyPawn`으로 바꾸면 그대로는 Player 공격이 Enemy를 찾지 못한다. 따라서 Player 공격 Sweep의 Object Query에 `EnemyPawn`도 추가해야 한다.

Enemy 공격은 예약된 Player를 직접 검증하는 Committed Target Cone 방식이므로 이 Object Type 변경의 직접적인 영향을 받지 않는다.

## 튜닝 순서

1. 먼저 충돌 채널만 바꾸고 Crowd 값은 그대로 둬서 원인을 분리한다.
2. 물리 끼임은 사라졌지만 멀리서부터 지나치게 감속하면 `Crowd Separation Weight`를 `2.0 -> 1.0~1.5` 범위에서 시험한다.
3. 너무 일찍 옆으로 비키면 `Crowd Collision Query Range`를 `500 -> 300~400 cm` 범위에서 시험한다.
4. Enemy가 서로 심하게 관통하면 Weight를 다시 높이기 전에 Slot 간격이 Capsule 지름보다 충분한지 확인한다.

## PIE 검증

- 12마리를 좁은 통로와 플레이어 주변에 배치한다.
- Enemy끼리 접촉해도 완전히 정지하거나 뒤로 튕기지 않는지 확인한다.
- Player는 Enemy에게 막히고 Enemy도 Player를 관통하지 않는지 확인한다.
- Player 공격이 기존과 동일하게 최대 3명의 Enemy를 타격하는지 확인한다.
- Wait/Holding의 정지 Enemy 사이를 이동 Enemy가 통과할 때 Mesh 겹침 시간과 버벅거림을 각각 관찰한다.
- 벽과 문틀은 이전처럼 확실히 Block하는지 확인한다.

## 결론

Enemy끼리 완전히 무관심하게 만드는 것이 아니라, **물리 충돌은 Ignore하고 군중 간격은 Detour Crowd가 담당하는 하이브리드 구조**가 현재 기획에 가장 적합하다.

## 참고

- [Epic - Collision Response Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/collision-response-reference-in-unreal-engine)
- [Epic - Add a Custom Object Type](https://dev.epicgames.com/documentation/unreal-engine/add-a-custom-object-type-to-your-project-in-unreal-engine)
- [Epic - UCrowdFollowingComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/UCrowdFollowingComponent)
- [[몬스터 이동 시스템 규칙]]
