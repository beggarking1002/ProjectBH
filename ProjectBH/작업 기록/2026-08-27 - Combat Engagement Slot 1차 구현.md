# Combat Engagement Slot 1차 구현

## 목표

- 플레이어 주변에 Attack Slot 4개와 Wait Slot 8개를 생성한다.
- 적은 플레이어 Actor 자체가 아니라 서버에서 예약한 슬롯의 NavMesh 위치로 이동한다.
- Attack Slot을 얻지 못한 적은 Wait Slot을 예약해 대기하며, 빈 Attack Slot이 생기면 승격할 수 있는 구조로 만든다.

## 구현 구조

### 플레이어: 슬롯 소유자

- `ABHHeroCharacter`가 `UCombatEngagementSlotComponent`를 기본 서브오브젝트로 소유한다.
- 컴포넌트가 서버 권한으로 Attack/Wait 예약자를 관리한다.
- 슬롯은 플레이어 회전에 따라 돌지 않는 월드 기준 원형 배치다.
- 각 슬롯 위치는 NavMesh 위로 투영된다.

### 적 AI: 슬롯 예약자

- `ABHCrowdEnemyAIController`는 가장 가까운 플레이어의 슬롯 컴포넌트를 조회한다.
- 빈 Attack Slot이 있으면 가장 가까운 유효 슬롯을 예약하고, 없으면 Wait Slot을 예약한다.
- 적은 `MoveToActor(Player)`가 아니라 `MoveToLocation(ReservedSlotLocation)`으로 이동한다.
- Wait Slot 보유자는 Attack Slot이 비면 다음 갱신 때 승격을 시도한다.
- Attack Slot 도착자만 기존 공격 사거리 조건을 통과한 뒤 공격한다.
- 공격 및 회복 중에는 Attack Slot을 유지한다.

## 기본값

| 항목 | 값 |
|---|---:|
| Attack Slot 수 | 4 |
| Wait Slot 수 | 8 |
| Attack Ring 반경 | 125 cm |
| Wait Ring 반경 | 300 cm |
| 슬롯 도착 허용 반경 | 15 cm |
| 이동 경로 재요청 거리 | 50 cm |
| AI 슬롯 갱신 주기 | 0.5초 |

모든 슬롯 수와 반경은 C++ 기본값이며 에디터의 클래스 기본값에서 조절할 수 있다.

## 예약 해제 조건

- 타깃 변경 또는 소실
- AI Controller의 UnPossess
- 예약 슬롯을 향한 경로 탐색 또는 이동 실패
- 예약한 Attack Slot이 공격 가능 거리 밖으로 투영된 경우
- 예약 Actor가 파괴되어 약한 참조가 무효화된 경우

부분 경로는 허용하지 않는다. NavMesh 위에 투영되었지만 실제로 연결되지 않은 슬롯을 적이 계속 점유하는 상황을 줄이기 위함이다.

## 디버그 표시

PIE 서버에서 플레이어 주변에 구체로 표시된다.

| 색 | 의미 |
|---|---|
| 초록 | 비어 있는 Attack Slot |
| 빨강 | 점유된 Attack Slot |
| 청록 | 비어 있는 Wait Slot |
| 노랑 | 점유된 Wait Slot |

## 검증 상태

- Unreal Header Tool 통과
- 변경된 C++ 파일 전체 컴파일 통과
- 에디터가 `UnrealEditor-ProjectBH.dll`을 사용 중이어서 최종 DLL 링크만 실패함
- 에디터를 완전히 종료한 뒤 다시 빌드하고 PIE 동작 검증 필요

## 에디터 확인 절차

1. Unreal Editor를 완전히 종료하고 프로젝트를 다시 빌드한다.
2. 테스트 구역의 NavMesh가 플레이어 기준 최소 300 cm 바깥까지 덮는지 `P` 키로 확인한다.
3. 같은 Enemy Blueprint를 12개 이상 배치한다.
4. PIE를 실행한다.
5. 4마리는 빨간 Attack Slot으로, 다음 8마리는 노란 Wait Slot으로 이동하는지 확인한다.
6. Attack Slot 적이 공격·회복 중 자리를 유지하고, 슬롯 해제 시 Wait 적이 승격하는지 확인한다.

## 현재 범위와 후속 작업

이번 단계는 슬롯 생성·예약·이동 연결까지다.

- Attack Slot 보유자는 공격이 끝나도 자리를 유지하므로, 최초 공격자 4명이 계속 공격한다.
- 공격자 교대 우선순위와 공정성은 다음 단계에서 정책을 정한다.
- 사망·경직 시 즉시 슬롯을 해제하는 명시적 이벤트 연동은 아직 없다. Actor 파괴 시에는 자동 정리된다.
- 같은 위치로 투영되는 슬롯 중복 방지와 다층 지형 판정은 평면 전투 테스트 이후 보강한다.
- 12개 슬롯이 모두 차면 추가 적은 슬롯이 생길 때까지 이동을 멈춘다.

## 관련 코드

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h/.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.h/.cpp`
- `Source/ProjectBH/BHHeroCharacter.h/.cpp`
