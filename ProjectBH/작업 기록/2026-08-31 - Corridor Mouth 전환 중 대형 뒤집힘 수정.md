# Corridor Mouth 전환 중 대형 뒤집힘 수정

> 후속 정정: 첨부 화면의 주된 문제는 Mouth 전환 자체가 아니라, 한쪽 Row만으로 총 Slot 수가 차면 반대쪽 탐색을 끝내는 전역 조밀 배치 규칙이었다. Mouth 증거 유지 기능은 남기되 Attack·Row 갱신 동결은 제거했다. 실제 양측 배치는 [[2026-08-31 - Corridor 양측 동시 Row 배치 수정]]을 따른다.

## 이해한 작업 범위

첨부 화면의 문제는 Corridor와 넓은 공간의 경계에서 `Mouth:Yes`가 검출됐는데도 확정 모드가 잠시 Corridor로 남아, Wait·Holding Row가 열린 갈래까지 다시 만들어지고 전체 대형이 한쪽 갈래로 뒤집히는 현상으로 이해한다. 일반 Corridor 내부의 동적 Row, 360도 Attack 후보, 동적 Side 전환과 Combat Core 우회 규칙은 유지한다.

## 화면에서 확인한 근거

- 두 화면 모두 굵은 빨간 Mouth 횡단면이 보인다. 이는 현재 코드 기준 `bCorridorMouthDetected == true`다.
- 동시에 Corridor용 하늘색·파란색 Row Slot이 넓은 공간의 한 갈래에 길게 남아 있다.
- 현재 구현은 Mouth 검출 후에도 `CorridorMouthExitDuration = 0.3초` 동안 확정 모드를 Corridor로 유지한다.
- 이 대기 중 Mouth 표본 하나가 누락되면 전환 누적 시간이 초기화되고, 다음 `가장 긴 축` 표본을 따라 Corridor 대형이 다른 갈래로 갱신될 수 있다.

## 수정 계약

1. Corridor에서 한 번 검출된 Mouth 증거는 짧은 시간 유지한다.
2. 유지 시간은 기본 `0.5초`로 하며, 한 번의 `0.2초` 공간 분석 표본 누락이 `0.3초` 이탈 확인을 취소하지 않게 한다.
3. 실제 Mouth 표본과 유지 중인 Mouth를 디버그 문자열에서 `Raw`, `Held`로 구분한다.
4. Corridor에서 Mouth 이탈을 확인하는 동안 대형 축은 새 탐침 방향으로 돌리지 않는다. Attack 후보와 Wait·Holding Row는 확정축을 기준으로 계속 갱신한다.
5. Mouth 전환이 완료돼 Open 또는 Pocket이 되면 기존 모드 전환 경로가 Corridor 대형을 정리한다.

## 비포함 범위

- Corridor 판정 임계값 전면 재조정
- Detour Crowd 또는 충돌 설정 변경
- Open·Pocket Slot 배치 변경
- Combat Core 우회 점수 변경

## 구현 결과

- 현재 표본의 Mouth와 유지 중인 Mouth를 분리했다.
- Corridor에서 Raw Mouth가 검출될 때마다 유지 시간을 `max(0.5초, Corridor Mouth Exit Duration)`으로 다시 설정한다.
- Raw와 Held는 모두 유효한 Mouth 증거다. 두 상태 모두 Corridor가 아닌 Candidate를 만들고 같은 `SpaceModeTransitionElapsed`를 계속 누적한다.
- 기본 분석 주기에서는 첫 Raw 표본이 이탈 누적에 약 `0.2초`를 더하고, 다음 Raw 또는 Held 표본에서 누적값이 `0.3초`를 넘어 Open 또는 Pocket 전환이 확정된다. `0.5초` 유지 시간은 전환 시간보다 길어 한 번의 누락 표본이 이 과정을 끊지 않게 한다.
- Mouth 이탈 확인 중에도 Attack 후보와 Wait·Holding Row를 갱신한다. 다만 Corridor 축 추종은 기존처럼 정지해 열린 갈래의 대각선 축으로 회전하지 않는다.
- 디버그 문자열은 `Mouth:Raw`, `Mouth:Held`, `Mouth:No`를 표시한다.

## 변경 위치

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
  - `CorridorMouthEvidenceHoldDuration`
  - Raw·Held 런타임 상태
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
  - `AnalyzeCombatSpace`
  - `DrawCombatSpaceAnalysisDebug`
- `ProjectBH/기획/몬스터 이동 시스템 규칙.md`
- `ProjectBH/기획/몬스터 이동 시스템 조작 및 수치 치트시트.md`

## 검증

- `git diff --check`: 통과
- UE 5.7 `ProjectBHEditor Win64 Development`: 빌드 성공
- PIE 시각 검증: 사용자 확인 필요

## PIE 확인 절차

1. `bh.Debug.Slots 1`을 켠다.
2. 문제를 재현한 Corridor와 넓은 공간의 경계로 천천히 이동한다.
3. 빨간 횡단선과 `Mouth:Raw`가 나온 뒤 한 표본이 흔들리면 `Mouth:Held`가 표시되는지 본다.
4. Raw와 Held 사이에 Corridor 축이 다른 갈래로 회전하지 않는지 본다. Row의 양측 균형은 후속 양측 Row 배치 규칙으로 확인한다.
5. 약 `0.3초` 뒤 `Space Open` 또는 `Space Pocket`으로 한 번만 전환되는지 본다.
6. 통로 중앙에서는 `Mouth:No`가 유지되고 기존 동적 Row·360도 Attack 배치가 그대로 동작하는지 회귀 확인한다.

`Mouth:Raw/Held` 문자열과 빨간 횡단선은 같은 코드 상태를 표현하므로 서로를 독립적으로 검증하지 못한다. 최종 합격은 디버그 문자열뿐 아니라 실제 Corridor 축과 Enemy 이동을 함께 보고 판단한다. Raw 또는 Held 구간에 축이 다른 갈래로 회전하지 않고, 모드 전환 뒤 Enemy가 새 Open·Pocket Slot으로 한 번만 재경로하면 통과다.
