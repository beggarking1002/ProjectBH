# NavMesh Bounds Volume 배치 및 검증

> 이 문서는 에디터 작업 절차만 다룬다. Enemy의 실제 추적·Slot·Crowd·교착 규칙과 현재 기본값은 [[몬스터 이동 시스템 규칙]]을 기준으로 한다.

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

## 경사로 입구를 무시하고 옆면으로 달려들 때

이 현상은 AI가 경사로 입구를 탐색하지 못하는 문제가 아니라, Recast NavMesh가 경사로 옆면의 높은 단차를 걸을 수 있는 연결로 잘못 생성했을 때 주로 발생한다.

1. `P`로 NavMesh를 표시하고 화면의 `AgentMaxStepHeight Low/Default/High` 값을 확인한다.
2. ProjectBH 기본값은 세 해상도 모두 `35 cm`다.
3. `Default`가 `200 cm`처럼 크게 표시되면 `RecastNavMesh-Default`를 선택한다.
4. Details에서 `NavMesh Resolution Params > Default > Agent Max Step Height`를 `35`로 되돌리거나 해당 Override의 Reset 화살표를 누른다.
5. `Build > Build Paths`로 NavMesh를 다시 생성한다.
6. 경사로 옆 수직면과 아래 바닥 사이에 녹색 폴리곤 연결이 없어졌는지 확인한다.
7. 실제 경사로 입구에서는 아래 바닥부터 경사면까지 녹색 영역이 연속이어야 한다.

입구 쪽 NavMesh가 끊겨도 `Agent Max Step Height`를 전역으로 크게 올리면 안 된다. 먼저 경사로 Collision의 턱·틈을 고치고, 의도적인 작은 단절만 남는다면 실제 입구에 한정해서 `Nav Link Proxy`를 사용한다. Nav Link를 경사로 옆면에 두면 같은 잘못된 지름길을 다시 만들 수 있다.

### 넓은 경사로의 장식 Collision 때문에 내부에 구멍이 생길 때

몬스터 떼가 사용하는 넓은 경사로라면 Point Nav Link를 여러 개 두는 것보다 경사로 보행면에 단순한 연속 Collision을 제공하는 방법을 우선한다.

1. 전역 `Agent Max Step Height`는 Enemy의 실제 `Character Movement > Max Step Height` 이하인 `35 cm`로 유지한다.
2. 경사로 위의 장식 판·철판이 별도 Static Mesh라면 해당 장식의 `Can Ever Affect Navigation`을 끈다.
3. 경사로 본체의 Collision이 울퉁불퉁하다면 얇은 `Blocking Volume` 여러 개 또는 숨겨진 단순 Collision Mesh로 실제 보행면을 덮는다.
4. 단순 Collision의 윗면은 보이는 경사로 표면과 맞추고, 옆 절벽이나 아래 바닥까지 닿지 않게 한다.
5. `Build Paths` 후 경사로 내부는 넓게 연속되고 옆 절벽 연결은 없는지 확인한다.

경사로 Mesh 전체가 장식과 난간 Collision까지 함께 담당한다면 Mesh 전체의 `Can Ever Affect Navigation`을 바로 끄지 않는다. 먼저 별도 Collision Mesh/Blocking Volume이 바닥과 난간을 필요한 범위만큼 대신하는지 확인해야 한다.

Collision 수정이 어려운 임시 대안은 구멍 양쪽의 실제 NavMesh를 `Nav Link Proxy`의 `Segment Links`로 잇는 것이다. Segment Link는 폭을 가진 연결을 만들 수 있어 Point Link보다 군중에 적합하지만, 많은 Enemy가 링크 전환 구간에 집중될 수 있으므로 연속 NavMesh보다 후순위다.

`Nav Modifier Volume`은 기존 NavMesh의 Area·타일 해상도는 바꿀 수 있지만 걸을 Collision 표면이 없는 구멍 자체를 생성하지는 않는다. High 해상도의 Step Height만 `200 cm`로 올려 경사로에 국소 적용하는 방식도 타일 전체가 옆 절벽을 포함하면 잘못된 연결이 재발하므로 권장하지 않는다.

## 참고

- [Epic Games: Basic Navigation](https://dev.epicgames.com/documentation/unreal-engine/basic-navigation-in-unreal-engine?lang=en-US)
- [Epic Games: Navigation System](https://dev.epicgames.com/documentation/unreal-engine/navigation-system-in-unreal-engine?lang=en-US)
