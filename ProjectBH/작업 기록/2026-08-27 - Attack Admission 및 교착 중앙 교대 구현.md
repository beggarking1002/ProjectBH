# 2026-08-27 - Attack Admission 및 교착 중앙 교대 구현

> PIE 후속 결과: 슬롯 각도 분리와 교착 교대만으로는 Attack 이동 경로를 통로에 고정하지 못했다. 남은 진입 버벅임의 원인과 후속 설계는 [[2026-08-28 - Attack 진입 Gate 필요성 분석]]에 정리했다.
>
> 후속 구현: Attack 이동 경로 강제는 [[2026-08-28 - Attack Approach Gate 단계 이동 구현]]에서 추가했다.

## 목적

후열 Enemy가 Attack Slot을 먼저 예약하거나 Wait Enemy가 진입선을 막아 Attack 예약자가 움직이지 못하는 문제를 줄인다. Attack 권한을 단순 등록 순서가 아닌 실제 진입 비용으로 배정하고, 교착 시 중앙 관리자가 역할을 교대한다.

## Codex 변경 사항

### Wait Ring 진입 통로

- `WaitRingAngleOffset` 기본값을 0도에서 22.5도로 변경했다.
- Attack Slot의 45, 135, 225, 315도 방사선과 Wait Slot이 직접 겹치지 않는다.
- Holding Ring의 11.25도 오프셋은 유지했다.

### 초기 Attack Admission

- 초기 등록 단계에서는 Enemy를 Wait, Holding 순으로 임시 배정한다.
- 마지막 신규 요청 등록 후 0.5초가 지나면 모든 임시 예약을 중앙에서 다시 구성한다.
- 전체 등록 Enemy와 빈 Attack Slot의 조합마다 동기 Nav 경로를 검사한다.
- 유효하고 완전한 경로 중 길이가 가장 짧은 Enemy-Slot 조합을 반복 선택해 Attack Slot을 최대 4개 채운다.
- 경로 길이가 1 cm 이내로 같으면 중앙 대기 Sequence가 빠른 Enemy를 우선한다.
- Attack 선정 후 남은 Enemy는 기존 Sequence 순서대로 Wait, Holding, Pending에 배정한다.
- 초기 재배정 시 `FormationRevision`을 증가시켜 Controller가 임시 MoveTo를 폐기하고 새 슬롯으로 갱신하게 한다.

### 런타임 Attack Admission

- Wait에서 Attack으로 승격할 때 더 이상 Sequence만으로 후보를 고르지 않는다.
- 다음 조건을 모두 만족하는 Wait Enemy만 후보가 된다.
  - 현재 Wait Slot에서 `PromotionArrivalRadius` 이내에 도착
  - 대상 Attack Slot까지 완전한 Nav 경로가 존재
  - Attack Slot이 Enemy 공격 사거리 안에 존재
  - Attack 재진입 쿨다운이 종료
- 후보 중 Nav 경로가 가장 짧은 조합을 우선하고, 동률일 때 Sequence를 사용한다.
- Holding에서 Wait 승격과 Pending에서 Holding 배정은 기존 중앙 Sequence 규칙을 유지한다.

### Attack 교착 중앙 교대

- 기존 2초 Stuck Watchdog이 Attack 예약자에게 발생하면 중앙 Slot Component가 교대를 먼저 시도한다.
- 도착한 Wait 후보 중 해당 Attack Slot까지 경로가 가장 짧은 Enemy를 선정한다.
- 한 서버 틱 안에서 Wait 후보를 Attack으로 올리고 교착 Enemy를 후보의 Wait Slot으로 내린다.
- 교착 Enemy의 중앙 Sequence는 제거하지 않는다.
- 강등된 Enemy는 `FailedSlotCooldown` 기본값인 2초 동안 Attack 후보에서 제외한다.
- 적합한 Wait 후보가 없으면 기존 방식대로 예약 반납과 실패 슬롯 제외를 사용한다.

## 사용자 에디터 작업

1. 에디터를 저장하고 종료한 뒤 Codex에게 최종 빌드 재실행을 요청한다.
2. Hero Blueprint에서 Combat Engagement Slot Component의 `Wait Ring Angle Offset`이 22.5인지 확인한다. 기존 Blueprint가 0을 명시적으로 저장했다면 22.5로 변경한다.
3. PIE 시작 직후 약 0.5초 동안 Attack이 임시로 비어 있을 수 있는지 확인한다.
4. `Phase:Runtime` 전환 시 물리적으로 경로가 가까운 네 Enemy가 Attack을 받는지 확인한다.
5. Attack 진입선 사이에 Wait 슬롯이 놓이고, Wait Enemy가 방사형 통로를 직접 막지 않는지 확인한다.
6. Attack Enemy의 앞을 여러 Enemy로 막아 2초 이상 정체시킨다.
7. 교착 Enemy가 Wait로 내려가고 이미 Wait에 도착한 후보가 같은 Attack Slot으로 올라가는지 확인한다.
8. 강등된 Enemy가 2초 안에 즉시 Attack으로 되돌아가지 않는지 확인한다.

## 검증

- `git diff --check` 통과
- Unreal Header Tool 성공
- 변경된 `BHCrowdEnemyAIController.cpp`, `CombatEngagementSlotComponent.cpp`, `BHEnemy.cpp` C++ 컴파일 성공
- 최종 DLL 링크는 열려 있는 Unreal Editor가 `UnrealEditor-ProjectBH.dll`을 사용 중이어서 `LNK1104`로 보류됐다. 컴파일 오류는 아니다.
- 에디터 종료 후 `ProjectBHEditor Win64 Development` 최종 링크 재검증이 필요하다.

## 관련 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`
- `ProjectBH/기획/군중 몬스터 이동 및 전투 자리 배정 설계 초안.md`
- `ProjectBH/작업 기록/2026-08-27 - Attack Slot 진입 교착 원인 분석.md`

## 남은 작업 / 다음 단계

- 에디터 종료 후 최종 링크를 완료한다.
- PIE에서 초기 경로 기반 선정과 런타임 교착 교대를 검증한다.
- 동기 경로 질의 비용은 초기 1회와 슬롯 승격 시점에 집중된다. 활성 Enemy 수를 크게 늘릴 때 프로파일링한다.
- 거리 우선 때문에 오래 기다린 Enemy가 계속 밀리는 현상이 보이면 Sequence 대기 시간에 따른 Aging 가중치를 추가한다.
