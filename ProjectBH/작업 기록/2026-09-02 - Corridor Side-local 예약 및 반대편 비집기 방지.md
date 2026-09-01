# Corridor Side-local 예약 및 반대편 비집기 방지

## 문제

Corridor는 Player 축 양쪽에 Slot을 만들고 Enemy의 안정 Side도 기록했지만, 실제 예약에서는 Side가 금지 조건이 아니라 선호값이었다.

- 같은 Side Wait 후보가 없으면 반대 Side 후보까지 Attack으로 승격했다.
- 초기·직접 Attack 예약과 전열 재편도 반대 Side를 허용했다.
- Wait·Holding의 구멍 압축은 Side를 무시하고 공통 배열의 낮은 인덱스부터 채웠다.
- Pending도 전체 Queue Prefix로 양쪽 Slot을 교대 사용했다.

NavMesh는 Player와 Crowd를 정적 장애물로 넣지 않으므로 이 배정들은 경로 생성에는 성공했다. 그러나 실제 이동에서는 Player Capsule과 Enemy 군집 사이를 통과하려다 Detour Crowd가 버벅이는 결과가 나왔다.

## 현행 규칙

1. Corridor Slot Layout은 양쪽에 계속 생성한다.
2. Enemy는 현재 실제 위치와 같은 Side의 Attack·Wait·Holding·Pending만 예약한다.
3. 같은 Side 수용량이 가득 차면 반대편으로 넘기지 않고 더 외곽 계층 또는 Pending에 남는다.
4. 반대편 Slot은 그쪽에서 스폰됐거나 외부 우회로를 통해 실제로 넘어간 Enemy가 사용할 수 있다.
5. Player 축에서 `60 cm` 이상 떨어졌을 때 실제 위치가 기록 Side보다 우선한다. 중심 `60 cm` 안에서는 기존 Side를 유지한다.
6. Attack Vacancy Fallback은 `0.25초` 뒤 도착 조건만 완화하고 Side 조건은 완화하지 않는다.
7. 빠른 Attack 인계, 교착 교대, 초기 배정, 직접 예약과 Reform도 Side-local이다.
8. 공격·회복 잠금 중인 기존 Attack Owner만 Montage 연속성을 위해 현재 Slot 보존을 우선한다.
9. 같은 Side 승격 후보가 없어 의도적으로 비워 둔 Attack Slot은 밝은 파랑과 상단 `SideHold` 수치로 표시한다. 기존 주황색 승격 지연과 구분한다.

## Queue 처리

- Queue Sequence는 Side별로 새로 만들지 않고 기존 Hero 공통 Sequence 하나를 유지한다.
- Wait·Holding 압축은 공통 Sequence를 Side별로 필터링한 뒤 각 Side 안쪽 Row부터 배치한다.
- 한 Side의 가장 오래된 Holding/Pending이 수용량 부족으로 승격할 수 없어도 다른 Side의 승격 후보를 계속 검사해 Head-of-line blocking을 막는다.
- Pending 목표는 같은 Side의 앞선 미예약 Requester 수로 Side-local 순위를 계산한다.

## 검증

- 좁은 Corridor 한쪽에만 Enemy를 스폰했을 때 반대편 Attack·Wait·Holding Slot이 남아 있어도 Enemy가 Player를 가로질러 이동하지 않아야 한다.
- 반대편에 별도 Enemy를 스폰하면 해당 Side Slot을 정상 점유해야 한다.
- 외부 우회 경로로 Enemy를 반대편 `60 cm` 밖까지 옮기면 다음 배정에서 그 Side Slot을 사용할 수 있어야 한다.
- 한쪽 수용량이 가득 차면 초과 Enemy가 Holding/Pending에 남고 반대편으로 이동하지 않아야 한다.
- 같은 Side Attack Vacancy는 기존 승격 속도로 채워지고, 반대 Side에 후보가 없는 Vacancy는 의도적으로 유지돼야 한다.

## 빌드

- `ProjectBHEditor Win64 Development -NoLink` C++ 컴파일 성공.

## 변경 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`
