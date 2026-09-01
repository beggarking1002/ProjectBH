# 포메이션 Catch-up Run 구현

## 문제

최초 포메이션 합류 여부만으로 Run을 끄면, Player가 도망가거나 Slot이 멀리 이동해도 이미 합류한 Enemy는 기존 Locomotion만 사용한다. 이력 기반 상태만으로는 현재 따라잡아야 하는 이동 의도를 표현할 수 없다.

## 결정

포메이션 합류 상태는 유지하되, 현재 예약 Slot까지의 거리로 별도의 Catch-up 상태를 계산한다.

```text
포메이션 미합류
    → Approach Run

포메이션 합류 + Slot 거리 300cm 이상
    → Catch-up Run

Catch-up 중 + Slot 거리 160cm 이하
    → 기존 Formation Locomotion
```

Player와의 직접 거리가 아니라 예약 Slot과의 거리를 기준으로 한다. 따라서 Open, Pocket, Corridor 및 Holding/Wait/Attack 배치에서도 같은 의미를 유지한다.

## 구현

- `ABHEnemy`
  - 복제 상태 `bNeedsFormationCatchUp` 추가
  - 새 교전 및 Object Pool 재활성화 시 초기화
- `ABHCrowdEnemyAIController`
  - `FormationCatchUpEnterDistance = 300cm`
  - `FormationCatchUpExitDistance = 160cm`
  - `FormationCatchUpMoveSpeed = 500cm/s`
  - Slot 거리 기반 히스테리시스 판단 추가
  - 접근·Catch-up 상태에서는 실제 MaxWalkSpeed도 Catch-up 속도로 설정
- `UBHEnemyAnimInstance`
  - `bNeedsFormationCatchUp`을 AnimBP에 공개
  - Run 조건을 다음과 같이 확장

```text
bIsChasing
&& bShouldMove
&& (!bHasJoinedFormation || bNeedsFormationCatchUp)
```

런타임 AI 디버그 문자열의 `Gait` 항목에서 `ApproachRun`, `CatchUpRun`, `Formation`을 확인할 수 있다.

## 에디터 작업

추가 ABP 수정은 없다. 기존 `Blend Poses by Bool`의 `Active Value`가 `bShouldUseRunLocomotion`에 연결되어 있으면 C++ 판단 변경을 자동으로 반영한다.

## 검증 상태

- `ProjectBHEditor Win64 Development` UHT, 컴파일, 링크 성공.
- PIE에서 Player 도주 시 Run 진입, Slot 접근 시 기존 Locomotion 복귀를 확인해야 한다.
