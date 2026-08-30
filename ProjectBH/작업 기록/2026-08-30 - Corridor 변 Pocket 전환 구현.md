# Corridor 변 Pocket 전환 구현

## 결정

Corridor 변에 선 Player를 위해 Corridor Attack Slot을 확장하지 않는다. 통로 중앙부만 Corridor로 사용하고, 어느 방향이든 한쪽 경계에 치우친 위치는 Pocket으로 전환한다.

이 결정에 따라 직전에 실험한 폭 축·대각선 Attack 후보와 Cross-Channel Attack 승격은 롤백했다.

## 판정 규칙

1. 기존 16방향 NavMesh 탐침의 모든 반대 방향 쌍을 검사한다.
2. 각 쌍에서 가까운 거리와 `먼 거리 - 가까운 거리`를 구한다.
3. 거리 차이가 가장 큰 쌍을 현재 변 후보로 사용한다.
4. 현재 Corridor이고 가까운 거리가 `100 cm 이하`, 거리 차이가 `100 cm 이상`인 상태가 `0.4초` 유지되면 Pocket으로 전환한다.
5. 이 규칙으로 들어온 Pocket에서는 가까운 거리 `130 cm 이하`, 거리 차이 `60 cm 이상`인 동안 Pocket을 유지한다.
6. 완화 조건을 벗어나 중앙 Corridor 조건을 만족하면 `0.6초` 후 Corridor로 복귀한다.

진입·유지 조건 차이는 판정 경계에서 상태가 반복 전환되는 것을 막는 히스테리시스다. 거리 차이 조건은 양쪽 벽이 모두 가까운 좁은 통로 중앙을 Pocket으로 오판하지 않기 위한 조건이다.

## 디버그

`bh.Debug.Slots 1`의 공간 문자열에 다음 값이 추가된다.

- `Edge:70/+430`: 가까운 경계까지 약 `70 cm`, 반대쪽에 `430 cm` 더 많은 여유가 있다는 뜻이다.
- 현재 모드가 Corridor이고 값이 `100 이하 / +100 이상`이면 Candidate가 Pocket으로 바뀌어야 한다.

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`
- `ProjectBH/기획/몬스터 이동 시스템 조작 및 수치 치트시트.md`

## PIE 확인

1. 통로 중앙에서 `Space Corridor`가 유지되는지 확인한다.
2. 통로의 폭 변 또는 긴 축 끝으로 이동한다.
3. `Edge`가 `100 이하 / +100 이상`이 되면 `Candidate:Pocket`이 표시되는지 확인한다.
4. 약 `0.4초` 뒤 `Space Pocket`으로 전환되고 Pocket 부채꼴 Slot이 만들어지는지 확인한다.
5. 다시 중앙으로 이동해 유지 기준을 벗어나면 약 `0.6초` 뒤 Corridor로 돌아오는지 확인한다.

## 빌드

- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공
- UnrealHeaderTool, C++ 컴파일, DLL 링크 성공
- Visual Studio 2022 컴파일러 선호 버전 경고만 있으며 이번 변경과 관련된 오류는 없다.

## 판정식 시뮬레이션 검증

현재 C++ 조건을 동일한 임계값으로 분리해 대표 공간을 입력했다. `기존 Pocket 조건`은 꺼진 상태다.

| 상황 | 가까운 거리 | 반대쪽 여유 차이 | Corridor형 | Edge Pocket | Candidate |
| --- | ---: | ---: | --- | --- | --- |
| 중앙 | `150` | `0` | 충족 | 불충족 | Corridor |
| 좁은 통로 변 | `70` | `160` | 충족 | 충족 | Pocket |
| 반대쪽이 열린 변 | `70` | `430` | 불충족 | 충족 | Pocket |
| 긴 축 끝 | `70` | `430` | 불충족 | 충족 | Pocket |
| 매우 좁은 중앙 | `80` | `0` | 충족 | 불충족 | Corridor |
| Pocket 경계 유지 | `120` | `60` | 충족 | 충족 | Pocket |
| Pocket 중앙 복귀 | `130` | `40` | 충족 | 불충족 | Corridor |

수정 후에는 Corridor 형태 조건이 무너지는 열린 변과 긴 축 끝도 Pocket으로 판정한다. 동시에 매우 좁은 중앙은 가까운 벽이 있어도 반대 방향 거리 차이가 없으므로 Corridor를 유지한다.
