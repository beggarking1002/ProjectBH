# 2026-08-28 - Object Pool SpawnPoint 자동 복구

## 증상

Object Pool을 사용하면 Enemy가 생성되지 않는 것처럼 보였다.

## 진단 근거

- PIE 로그에는 Pool이 `Spawned:40 Alive:12 Free:28 ActiveLimit:12`로 초기화됐다고 기록되어 있었다.
- 따라서 Actor 생성과 초기 활성화 자체는 실패하지 않았다.
- 현재 `BP_BHEnemyPoolManager` 인스턴스의 External Actor 데이터에는 `SpawnPoints` 참조가 직렬화되어 있지 않았다. 참조가 빠진 원인 자체는 이 진단에서 확정하지 않았다.
- 같은 맵에는 TargetPoint Actor 한 개가 존재했다.
- 결과적으로 Pool은 TargetPoint가 아니라 Manager 주변 Fallback Grid를 사용해 Enemy를 내보내고 있었다.
- 종료 시점의 `Unable to find RecastNavMesh` 경고는 World 정리 중 발생했으며 Pool 초기화 실패 원인은 아니었다.

## 수정

`ABHEnemyPoolManager::BeginPlay`에서 Pool을 만들기 전에 Spawn Point를 해석한다.

1. 배열에서 무효한 Actor 참조를 제거한다.
2. 명시적으로 등록된 유효 Spawn Point가 있으면 그대로 사용한다.
3. 배열이 비어 있고 `bAutoDiscoverTargetPointsWhenEmpty`가 켜져 있으면 현재 World의 모든 `ATargetPoint`를 찾는다.
4. 발견한 TargetPoint를 런타임 Object Name순으로 정렬해 같은 World 구성에서 순환 순서를 고정한다.
5. Point 수가 Active Limit보다 적으면 같은 Point의 반복 사용을 `175 cm` Grid로 분산한다.
6. TargetPoint도 없을 때만 기존 Manager 중심 Fallback Grid를 사용하고 경고를 남긴다.

기본값은 자동 발견과 반복 Point 분산 모두 활성이다. 명시적 배열이 우선하므로 기존 레벨의 Spawn Point 선택은 바뀌지 않지만, Point를 반복 사용하는 경우에는 겹침 방지를 위한 위치 Offset이 적용된다.

## 빌드 검증

- UE 5.7 UnrealHeaderTool 성공
- `BHEnemyPoolManager.cpp` 컴파일 성공
- `UnrealEditor-ProjectBH.dll` 링크 성공
- 결과: `Succeeded`
- 남은 경고는 프로젝트 기존 Visual Studio 14.38 비권장 경고뿐이다.

## 런타임 검증

FeatureDevMap을 UI 없이 실제 Game World로 로드했다.

- `auto-discovered 1 TargetPoint spawn point(s).`
- `initialized enemy pool. Spawned:40 Alive:12 Free:28 ActiveLimit:12`
- 프로세스 종료 코드 `0`

즉 `TargetPoint 자동 발견 -> 40개 Prewarm -> 12개 활성화` 경로가 실제 맵에서 이어지는 것을 확인했다. 이 헤드리스 검증은 Actor 활성화까지 확인하며, 화면에 보이는 위치와 연출은 PIE에서 별도로 확인해야 한다.

헤드리스 로그에는 Pool과 무관한 `FBHInputActionConfig::InputAction is not initialized properly` 오류가 별도로 남아 있다. 이번 Pool 수정 범위에는 포함하지 않았다.

## 사용자 확인

1. Unreal Editor를 다시 열고 FeatureDevMap에서 PIE를 실행한다.
2. Pool Manager를 선택하고 `Enemy Pool|Spawn`에서 `Auto Discover Target Points When Empty`와 `Spread Repeated Spawn Points`가 켜져 있는지 확인한다.
3. Output Log의 `LogProjectBH`에서 `auto-discovered 1 TargetPoint spawn point(s).`를 확인한다.
4. PIE를 Pause하고 World Outliner에서 런타임 `BP_BHEnemy` Actor 40개가 존재하는지 확인한다. 이 확인은 Pool 자체의 카운트 로그와 독립적이다.
5. Viewport에서 한 개의 TargetPoint 주변에 활성 Enemy 12마리가 `175 cm` 간격 Grid로 나타나는지 확인한다. 헤드리스 로그만으로 이 시각 결과를 통과 처리하지 않는다.
6. 활성 Enemy 하나를 선택해 Hidden 상태가 아니며 TargetPoint 주변 좌표에 있는지 Details에서 확인한다.
7. Pool Manager의 `Spawn Points`가 비어 있어도 자동 발견 옵션이 켜져 있으면 BeginPlay 시 현재 World에 로드된 모든 TargetPoint를 사용한다. Enemy 전용이 아닌 TargetPoint가 추가되면 명시적 배열을 사용한다.
8. 여러 Spawn 위치를 정확히 통제하려면 각 TargetPoint를 `Spawn Points`에 명시적으로 등록한다.

## 관련 파일

- `Source/ProjectBH/Enemies/BHEnemyPoolManager.h/.cpp`
- [[몬스터 이동 시스템 규칙]]
- [[2026-08-28 - Enemy 전용 충돌 채널 구현]]
