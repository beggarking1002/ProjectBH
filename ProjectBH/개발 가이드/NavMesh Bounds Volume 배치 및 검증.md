# NavMesh Bounds Volume 배치 및 검증

## 목적

테스트 맵에서 Enemy가 UE NavMesh를 사용해 플레이어를 추적할 수 있도록 이동 가능 영역을 생성하고 검증한다.

## 배치 절차

1. 이동을 테스트할 레벨을 연다.
2. 에디터 상단의 `Add(+)` 또는 `Place Actors` 패널을 연다.
3. 검색창에 `Nav Mesh Bounds Volume`을 입력한다.
4. `Nav Mesh Bounds Volume`을 뷰포트로 드래그한다.
5. 볼륨을 선택한 뒤 이동·크기 조절 도구로 전투 공간을 감싼다.
   - 가로와 세로는 Enemy와 플레이어가 움직일 바닥 전체를 덮는다.
   - 높이는 바닥과 캐릭터의 Capsule이 볼륨 안에 포함될 만큼 확보한다.
   - 볼륨의 아래쪽이 바닥과 교차하도록 둔다. 바닥 위에 떠 있게 두지 않는다.
6. 뷰포트에 포커스를 두고 `P` 키를 눌러 Navigation 표시를 켠다.
7. 걸을 수 있는 바닥이 녹색으로 표시되는지 확인한다.

NavMesh는 볼륨 자체를 걷는 것이 아니라, 볼륨 내부에 있는 충돌 지형을 기준으로 생성된다. 따라서 바닥 Static Mesh에 적절한 Collision이 있어야 한다.

## ProjectBH 첫 테스트 권장 범위

- 첫 테스트에서는 `NavMeshBoundsVolume` 하나만 사용한다.
- 작은 평지형 전투장을 전부 덮는다.
- `BP_Enemy_Base`와 플레이어 시작 위치 양쪽 아래에 녹색 영역이 있어야 한다.
- 두 지점 사이의 녹색 영역이 끊기지 않아야 한다.
- Enemy는 녹색 영역 위에 배치한다.

## Enemy 설정 확인

`BP_Enemy_Base`의 Class Defaults에서 다음 값을 확인한다.

- `AI Controller Class`: `BHCrowdEnemyAIController`
- `Auto Possess AI`: `Placed in World or Spawned`
- `Walk Left for Animation Preview`: false

기존 Blueprint가 과거 기본값을 저장했다면 C++ 기본값과 다를 수 있으므로 직접 확인한다.

## 정상 동작 기준

PIE 실행 시 다음 조건을 만족해야 한다.

1. Enemy가 플레이어를 향해 이동한다.
2. 장애물이 있으면 녹색 NavMesh가 이어진 경로로 우회한다.
3. 플레이어 약 150cm 앞에서 멈춘다.
4. 플레이어가 멀어지면 추적을 다시 시작한다.
5. Enemy AnimBP의 `GroundSpeed`, `Direction`, `bHasAcceleration`이 이동 상태를 반영한다.

## 녹색 영역이 보이지 않을 때

1. 뷰포트에 포커스를 둔 상태에서 `P`를 다시 누른다.
2. 볼륨의 크기와 위치가 바닥을 실제로 감싸는지 확인한다.
3. 바닥 Static Mesh의 Collision이 존재하는지 확인한다.
4. World Outliner에 `RecastNavMesh-Default`가 생성됐는지 확인한다.
5. `Build > Build Paths`를 실행한다.
6. 그래도 생성되지 않으면 `Project Settings > Navigation System`의 `Auto Create Navigation Data`를 확인한다.

## 녹색 영역이 중간에 끊길 때

- 문이나 통로 폭이 Enemy의 Nav Agent/Capsule 반지름보다 좁은지 확인한다.
- 바닥 사이에 실제 Collision 틈이나 높이 차이가 있는지 확인한다.
- 이동을 막으면 안 되는 Mesh의 `Can Ever Affect Navigation` 설정을 확인한다.
- 첫 테스트에서는 복잡한 계단보다 평평한 바닥에서 먼저 연결을 검증한다.

## 참고

- [Epic Games: Basic Navigation](https://dev.epicgames.com/documentation/unreal-engine/basic-navigation-in-unreal-engine?lang=en-US)
- [Epic Games: Navigation System](https://dev.epicgames.com/documentation/unreal-engine/navigation-system-in-unreal-engine?lang=en-US)
