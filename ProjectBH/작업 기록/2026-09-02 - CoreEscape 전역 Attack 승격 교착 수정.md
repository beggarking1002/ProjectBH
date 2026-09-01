# CoreEscape 전역 Attack 승격 교착 수정

## 증상

- 활성 Attack Slot이 모두 주황색으로 유지됐다.
- 상단 디버그는 `A:0/4`, `Blocked:0`, `W:8/8`, `VacA:9.7`, `Promote:CoreEscape`를 표시했다.
- Enemy 군집 안의 한 Requester가 `Route:CoreEscape`를 오래 유지했다.

주황색은 예약 불가 Slot이 아니라 `AttackVacancyFallbackDelay`가 지난 비어 있는 활성 Attack Slot이다. `Blocked:0`이므로 대형 Enemy의 Cost·Exclusion 정책도 원인이 아니었다.

## 직접 원인

`RefreshPromotions()`가 `HasActiveCombatCoreEscape()`인 동안 모든 Wait→Attack 정상 승격과 빈자리 fallback을 전역 중단했다. 따라서 군집 속 단 한 마리가 CoreEscape에서 지연되면 다른 도달 가능한 Wait Enemy가 있어도 전체 Attack 전열이 무기한 비었다.

추가로 Controller는 최종 Slot 좌표와의 거리만으로 도착을 판정했다. `CoreEscape` 같은 중간 Route Stage가 남아 있어도 최종 Slot과 가까우면 이동을 멈추고 Watchdog을 초기화할 수 있어 전역 잠금이 더 오래 유지될 가능성이 있었다.

## 수정

1. CoreEscape 전역 승격 게이트를 제거했다.
2. CoreEscape는 해당 Requester의 로컬 이동 상태로만 유지한다.
3. 다른 Wait Enemy는 정상 승격 및 `0.25초` 빈자리 fallback으로 Attack Slot을 채운다.
4. `ApproachRing`, `AlignOnRing`, `BypassCorePositive/Negative`, `CoreEscape` 중에는 최종 Slot과 가까워도 Slot 도착으로 판정하지 않는다.
5. 중간 Waypoint를 끝낸 뒤 `Direct` 또는 `Ingress` 최종 이동 단계에서만 정지한다.
6. 디버그의 `Promote:CoreEscape`는 승격 차단으로 오해되지 않도록 `Promote:Ready/CoreLocal`로 변경했다.

## 기대 불변식

- `Blocked:0`, 점유된 Wait 존재, 완전한 Attack 경로 존재, vacancy fallback 경과 조건이면 한 Requester의 CoreEscape와 무관하게 Attack 예약이 생성된다.
- CoreEscape Requester의 경로 실패는 해당 Requester의 복구 문제이며 다른 Attack Slot의 예약 가능성을 잠그지 않는다.
- Slot 승격은 Enemy끼리의 물리 중첩을 직접 해결하지 않지만, 군집 하나가 전열 전체를 정지시키는 연쇄 고장은 방지한다.

## 검증 항목

- 기존 재현 위치에서 `VacA`가 `0.25초`를 넘긴 뒤에도 `A:0/4`가 유지되지 않는지 확인한다.
- CoreEscape Enemy가 남아 있는 동안 상단이 `Promote:Ready/CoreLocal`을 표시하고 다른 Wait Enemy가 Attack으로 승격되는지 확인한다.
- 중간 Route Stage Enemy가 최종 Slot 근처에서 정지하지 않고 주어진 Waypoint까지 계속 이동하는지 확인한다.
- Attack Slot 점유 수가 복구된 뒤 CoreEscape Enemy도 바깥으로 탈출하거나 기존 Watchdog 복구로 넘어가는지 확인한다.

## 빌드 검증

- 변경된 `BHCrowdEnemyAIController.cpp`와 `CombatEngagementSlotComponent.cpp` 컴파일 성공.
- `ProjectBHEditor Win64 Development -NoLink` 검증 성공.
- 전체 링크는 실행 중인 Unreal Editor가 `UnrealEditor-ProjectBH.dll`을 점유해 수행하지 못했다. 에디터 종료 후 일반 빌드 또는 재실행이 필요하다.

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`
