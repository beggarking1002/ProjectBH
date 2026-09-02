# Reserve Layer 거리 기반 직행 배정

## 문제

기존 Runtime 승격은 `Wait → Attack`, `Holding → Wait`, `Pending → Holding`을 한 단계씩 처리했다. Holding Enemy는 자기 파란 Slot에 `60 cm` 이내로 도착해야 Wait로 승격될 수 있었다.

따라서 Attack 승격으로 Wait가 비어도 다음 Enemy가 먼저 Holding까지 이동해 정지한 뒤 다시 Wait로 출발했다. 화면에서는 가까운 Enemy가 바깥 파란 Slot에 멈추고 먼 Enemy가 뒤늦게 빈 Wait 방향으로 이동하여 대형 합류가 일사불란하지 않게 보였다.

## 변경 규칙

1. `Wait → Attack`의 공격 공정성과 도착·Cooldown 규칙은 유지한다.
2. 빈 Wait는 Holding과 Pending을 합친 외곽 후보군에서 직접 채운다.
3. 모든 `외곽 Enemy × 빈 Wait` 조합의 완전한 Nav 경로와 혼잡 패널티를 비교한다.
4. 경로 비용이 가장 낮은 조합을 선택하고, `1 cm` 이내 동점일 때 Queue Sequence를 사용한다.
5. Holding 도착 조건을 제거하여 중간 파란 Slot 정차 없이 Wait로 목적지를 바꾼다.
6. 남은 Pending은 모든 `Pending Enemy × 빈 Holding` 조합 중 경로 비용이 가장 낮은 조합부터 배치한다.
7. Corridor는 모든 비교에서 같은 물리 Side만 허용한다.
8. Corridor Row 토폴로지 재배치도 Queue 순번 고정 대신 같은 Side 내 남은 이동 거리 최소 조합을 사용한다.

## 기대 결과

- Attack을 채운 뒤 가까운 Enemy가 Wait를 먼저 채운다.
- 더 먼 Enemy는 남아 있는 방향의 Holding 또는 Pending 위치로 분산된다.
- Holding을 경유지처럼 찍고 멈춘 뒤 Wait로 이동하는 타일식 움직임이 사라진다.
- Queue Sequence는 사라지지 않으며 Attack 승격 우선권과 경로 비용 동점 처리에 계속 사용된다.
- Corridor 반대편 비집기 방지 규칙은 유지된다.

## 검증

- 여러 Enemy를 한 방향에서 동시에 접근시켜 Attack 이후 Wait가 거리 순으로 빠르게 채워지는지 확인한다.
- Wait 승격 직후 Holding 이동 중인 Enemy가 파란 Slot에서 정차하지 않고 새 Wait로 바로 방향을 바꾸는지 확인한다.
- 먼 Enemy가 비어 있는 외곽 방향으로 분산되는지 확인한다.
- Corridor에서 반대 Side의 가까운 Enemy가 있어도 Player를 가로질러 Wait를 받지 않는지 확인한다.

## 빌드

- `ProjectBHEditor Win64 Development -NoLink` C++ 컴파일 성공.

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`
