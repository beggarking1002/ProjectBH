# Combat Core 침입 및 Attack 수용량 문제 재정의

## 우선순위 변경

Safe Formation Center와 전역 투영 Slot 중복 제거는 정상 지형에서 체감 변화가 작고 현재 전투 루프를 직접 깨뜨리는 문제는 아니다. 두 작업은 롤백 상태로 유지하고 다음 두 문제를 먼저 해결한다.

1. Player의 급격한 방향 전환으로 Combat Core 내부에 남은 Enemy의 복구
2. Pocket·Corridor의 실제 공간보다 많은 Attack Slot 배치와 승격 정체

## 문제 1: Combat Core 내부 Enemy 복구 부재

### 재현

1. Player가 한쪽 방향으로 도망가 Enemy 대형을 끌고 간다.
2. Player가 반대 방향으로 급격히 전환한다.
3. 추격하던 Enemy 하나가 새 Player 중심의 Combat Core 내부에 남는다.
4. 다른 Enemy가 Attack Slot을 채우면 내부 Enemy가 바깥으로 빠져나가지 못하고 비정상 이동 또는 정지 상태가 된다.

### 현재 구조의 문제

- 이동 경로 코드는 선분이 Combat Core를 가로지르는지는 검사하지만 Enemy가 이미 Core 안쪽에 있는 상태를 별도 행동 상태로 취급하지 않는다.
- 기존 우회는 자신의 예약 Slot으로 가기 위한 보조 경로이며, Core 탈출 자체가 최우선 목표가 아니다.
- NavMesh 경로는 정지한 Attack Slot Enemy를 동적 벽으로 보지 않으므로 논리상 유효한 바깥 경로가 실제 군중에서는 막힐 수 있다.
- 기존 Watchdog은 일정 시간 저속·무진행 상태가 되어야 동작한다. Enemy가 앞뒤로 조금씩 움직이거나 Formation 변경으로 목표가 갱신되면 Stuck 시간이 초기화될 수 있다.
- Slot을 반납하고 다시 받아도 Core 안쪽이라는 공간 문제가 그대로면 같은 실패를 반복한다.

### 보완 방향

- Controller에 명시적인 `CoreEscape` 이동 단계를 둔다.
- Core 내부 진입과 탈출에 서로 다른 반경을 사용해 경계 왕복을 막는다.
- CoreEscape 중에는 기존 Slot 도착보다 탈출 지점을 우선한다.
- Player 주위 여러 방향의 바깥 후보를 검사하고 NavMesh 경로, 벽 여유, 현재 Attack 점유자와의 간격을 점수화해 가장 안전한 출구를 고른다.
- 탈출 중 Slot을 반납하더라도 Queue Sequence는 유지한다.
- Core 바깥의 탈출 완료 반경에 도달한 뒤 일반 Slot 요청으로 복귀한다.
- 디버그에 `Route:CoreEscape`, 탈출 후보와 선택 출구를 표시한다.

### 1차 구현 결과

- `EBHCombatMoveRouteStage::CoreEscape`를 추가했다.
- Enemy 중심과 Player 중심의 2D 거리가 유효 Combat Core `100 cm` 미만이면 공간 모드와 기존 Slot 종류보다 탈출 경로를 우선한다.
- 같은 두 중심의 거리가 `200 cm` 이상이 될 때까지 `CoreEscape`를 유지하는 히스테리시스를 적용했다.
- 현재 방사 방향 기준 `16방향`에서 NavMesh 투영, 주변 여유, 완전 경로를 검사한다.
- 탈출 선분에서 `105 cm` 안에 있는 현재 Attack 점유자 수를 세어 점유자가 적은 출구를 우선한다.
- 정상 탈출 중에는 기존 Slot 예약과 Queue Sequence를 유지한다. Slot 이동 목표만 탈출 목표로 대체한다.
- Core 내부 또는 탈출 중인 Enemy가 있으면 해당 Player의 Slot Manager 전체에서 새 Wait→Attack 승격을 중지한다. 다수 침입자는 모두 탈출해야 승격을 재개한다.
- 탈출 중에는 Attack 교착 교대를 사용하지 않고, 경로 실패·교착으로 예약을 반납할 때도 기존 Queue Sequence를 보존한다.
- Crowd 디버그에서 `Route:CoreEscape`와 연두색 선택 출구를 확인할 수 있다.
- Unreal Engine 5.7 `ProjectBHEditor Win64 Development` 빌드에 성공했다.

## 문제 2: 실제 공간과 Attack 수용량 불일치

### Pocket의 직접 원인

- Pocket Attack 수용량은 현재 열린 호의 각도와 `95 cm` 간격만으로 계산한다.
- 각 Attack 후보의 실제 벽 여유, 투영 오차와 접근 통로를 수용량 계산 단계에서 완전히 검증하지 않는다.
- 개별 Pocket 위치 생성이 실패하면 각도를 중앙 방향으로 여러 번 축소하고 마지막에는 중앙 후보로 투영한다.
- 따라서 논리적으로는 여러 Slot이지만 실제로는 같은 좁은 중앙 공간으로 몰리는 상황이 생길 수 있다.
- 활성 Attack 수가 최소 1로 고정되어 완전히 안전한 Attack 위치가 없는 경우도 표현하지 못한다.

### Corridor의 남은 문제

- Corridor Attack은 이미 360도 후보의 NavMesh 투영, 벽 여유와 후보 간 간격을 검사한다.
- 하지만 후보 최대 수를 우선하며 실제 Wait Enemy가 정지한 Attack 점유자 사이를 통과할 수 있는지는 정적 Nav 경로만으로 충분히 판정하지 못한다.
- 공격·회복 중인 예약이 있으면 Layout 축소를 보류하므로 순간적인 과수용 상태가 오래 유지될 수 있다.
- 후보 벽 여유는 기본 Agent 반경 `45 cm`, 후보 간격은 `90 cm`라 실제 군중 조향 여유가 거의 없다.

### 보완 방향

- Attack 수를 먼저 정한 뒤 Slot을 밀어 넣지 않고, 실제로 유효한 Attack 위치 목록을 만든 결과가 수용량이 되게 한다.
- Pocket에도 Corridor와 같은 명시적 Attack Layout을 만든다.
- Pocket Attack 후보에는 투영 오차, Capsule 반경과 안전 여유, 벽 Raycast, 후보 간 최소 간격, Player까지의 직접 접근 가능성을 검사한다.
- Attack 후보 실패 시 중앙으로 강제 압축하지 않는다. 해당 후보를 제거하고 `0개` 수용량도 허용한다.
- 공격·회복 중인 기존 Enemy는 즉시 밀어내지 않되, 새 물리 수용량보다 많은 동안 추가 승격을 중지한다.
- Wait에서 Attack으로 승격할 때 실제 Requester의 완전 경로와 진입 구간 혼잡을 다시 검사한다.
- Corridor는 Candidate 수뿐 아니라 진입 가능한 Candidate 수를 별도 계측한다.

## 권장 구현 순서

1. `CoreEscape` 상태와 탈출 완료 조건 구현: **완료**
2. CoreEscape 중 Queue 보존 및 Attack 승격 간섭 차단: **완료**
3. 급반전 4·8·12마리 PIE 검증: **사용자 확인 필요**
4. Pocket Attack을 개수 계산 방식에서 실제 후보 Layout 방식으로 교체한다.
5. Pocket 중앙 강제 압축 fallback을 Attack 계층에서 제거하고 0개 수용량을 허용한다.
6. Corridor·Pocket 공통으로 승격 시 실제 진입 가능성을 검사한다.

## 합격 기준

- Player 급반전 후 Core 내부 Enemy가 다른 Attack 점유자 사이에서 영구 정지하지 않고 바깥으로 탈출한다.
- 탈출 후 기존 Queue Sequence를 유지하고 정상 Slot 계층에 다시 합류한다.
- Pocket·Corridor에서 실제 공간이 부족하면 Attack Slot 수가 4개보다 자연스럽게 줄어든다.
- 안전한 Attack 후보가 없으면 빈 Attack Slot을 억지로 유지하지 않고 승격을 잠시 멈춘다.
- 벽에 가까운 Attack Slot과 동일 중앙 위치로 압축된 Attack Slot이 생성되지 않는다.
