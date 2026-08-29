# 2026-08-28 - EnemyPawn 지형 관통 수정

> 상태: EnemyPawn 전용 충돌 실험 전체가 2026-08-29 롤백됨. 현재 명세는 [[몬스터 이동 시스템 규칙]] 참고.

## 증상

Enemy가 바닥 위에 서지 못하고 아래로 떨어졌다.

## 원인

전용 Object Channel `EnemyPawn`의 기본 반응이 Ignore였다.

- Enemy Capsule의 `BHEnemyCapsule` Profile은 WorldStatic과 WorldDynamic을 Block했다.
- 그러나 기존 바닥 Component는 새 EnemyPawn 채널에 기본 Ignore로 반응했다.
- Unreal Collision은 양쪽 Component의 반응을 함께 해석하므로 한쪽이 Ignore하면 최종 상호작용도 Ignore가 된다.
- 결과적으로 Character Movement Capsule이 바닥을 통과했다.

## 수정

- `EnemyPawn` Object Channel의 Default Response를 `Block`으로 변경했다.
- `BHEnemyCapsule`의 `EnemyPawn -> EnemyPawn = Ignore`는 그대로 유지했다.
- 따라서 바닥·벽·Player는 Enemy를 Block하고 Enemy끼리만 하드 충돌을 무시한다.
- `CharacterMesh`, Ragdoll, Spectator는 EnemyPawn을 Ignore하도록 엔진 Profile 예외를 추가했다.
- Trigger, Overlap, UI Profile은 EnemyPawn을 Overlap하도록 예외를 추가했다.
- 이에 따라 새 채널의 기본 Block이 Mesh 충돌이나 Trigger 동작을 하드 Block으로 바꾸지 않는다.

## 런타임 검증

FeatureDevMap을 헤드리스 Game World로 실행했다.

- TargetPoint 1개 자동 발견
- Pool `Spawned:40 Alive:12 Free:28 ActiveLimit:12`
- Initial Formation 배정 완료
- 활성 Enemy의 공격 시작 확인
- `Fell Out Of World`, KillZ, Pool Activation 실패 로그 없음

Enemy가 이동·Formation·공격 단계까지 진행했으므로 Capsule과 지형의 충돌이 복구된 것을 확인했다. 최종 시각 확인은 PIE에서 수행한다.

## 에디터 확인

Collision Channel 설정은 시작 시 로드되므로 Unreal Editor를 완전히 종료했다가 다시 실행한다.

1. `Project Settings -> Engine -> Collision`에서 `EnemyPawn`의 Default Response가 Block인지 확인한다.
2. `BHEnemyCapsule`에서 EnemyPawn만 Ignore인지 확인한다.
3. PIE에서 Enemy가 바닥에 서는지 확인한다.
4. Enemy끼리는 여전히 서로 밀어내지 않는지 확인한다.
5. Player·벽과는 계속 충돌하는지 확인한다.

## 관련 파일

- `Config/DefaultEngine.ini`
- `Source/ProjectBH/Collision/BHCollisionChannels.h`
- [[몬스터 이동 시스템 규칙]]
- [[2026-08-28 - Enemy 전용 충돌 채널 구현]]
