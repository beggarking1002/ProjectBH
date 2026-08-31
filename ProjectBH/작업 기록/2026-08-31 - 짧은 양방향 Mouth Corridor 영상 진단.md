# 짧은 양방향 Mouth Corridor 영상 진단

## 입력

- 영상: `bandicam 2026-08-31 14-08-25-903.mp4`
- 길이: 약 `30.5초`
- 맵: `FeatureDevMap`
- 관찰 위치: 두 넓은 공간 사이의 짧은 연결 통로

## 관찰

- Player가 같은 짧은 구간에서 조금씩 이동하는 동안 Wait·Holding 대형 전체가 한쪽 열린 공간과 반대쪽 열린 공간 사이를 반복해서 오간다.
- 대표적으로 약 `7.5~10초`, `15~20초`와 `12.5초`, `25초`의 Slot 분포가 서로 반대쪽에 형성된다.
- 화면에는 Player 앞·뒤의 횡단면 선이 모두 넓게 보인다. 현재 디버그는 전체 Mouth Bool 하나로 두 선을 같은 색으로 칠하므로, 빨간 선 두 개만으로 전방·후방이 각각 Mouth 임계값을 통과했다고 확정할 수는 없다.
- 대표 프레임의 상단 점유 수는 `W:0/8 H:0/16 Q:0`인데도 빈 Slot 구체의 분포가 한쪽으로 바뀐다. 따라서 이 영상의 반전은 Enemy 점유 순서만으로 생긴 현상이 아니라, 표시되는 Layout 위치 생성 또는 공간 모드 전환 단계에서 이미 발생한다.

영상에서는 공간 상태 문자열과 계층별 `RowSides`가 안정적으로 읽히지 않는다. 따라서 공간 모드 재진입과 양쪽 후보 수 변화는 아래 코드 구조를 근거로 한 유력 가설이며 아직 런타임 수치로 확정하지 않았다.

## 진단

이 지형은 현재 `Open / Corridor / Pocket` 중 하나로 안정적으로 표현하기 어렵다.

- `Open`이라고 보기에는 Player 바로 옆에 통로 벽이 있다.
- 일반 `Corridor`라고 보기에는 첫 Wait Row 거리 안에서 축 양쪽이 모두 넓은 공간으로 열린다.
- `Pocket`이라고 보기에는 열린 방향이 하나가 아니라 서로 반대인 두 방향이다.

현재 Corridor 분석은 하나의 확정축과 하나의 `Mouth 여부`를 사용한다. 짧은 통로에서는 전후 횡단면과 축 측정 결과가 작은 Player 이동으로 바뀔 수 있다. 코드상 다음 현상이 결합할 가능성이 높다.

1. 공간 모드의 Corridor 이탈·재진입 후보가 흔들릴 수 있다.
2. Corridor에 다시 들어올 때 확정축의 방향 또는 부호가 새로 선택될 수 있다.
3. 단일 축을 따라 만든 한쪽 Row 중심만 직접 Nav 유효성을 통과하고 반대쪽은 실패할 수 있다.
4. 양측 균형 할당은 유효 후보를 나누는 규칙이므로 후보가 없는 Side에 가짜 Slot을 만들 수 없다.

따라서 `Wait 4:4 / Holding 8:8` 할당 규칙은 정상적인 긴 Corridor에는 유효하지만, 이 짧은 연결 통로의 공간 후보 생성 문제까지 해결하지는 못한다.

## 요구사항 보정: Mouth 양면 균형 대형

- Mouth 앞에 있다는 이유로 Enemy가 열린 공간 또는 Mouth 쪽에만 모여서는 안 된다.
- 긴 Corridor의 단일 Mouth에서도 Player 기준 통로 안쪽과 열린 공간 쪽에 Enemy가 균등하게 배치되어야 한다.
- 따라서 `Double Mouth Connector`는 짧은 양방향 통로를 위한 보조 하위 상태일 수는 있지만, 문제 해결의 중심 규칙은 아니다.
- 중심 규칙은 양쪽 공간의 형태가 달라도 두 전선을 독립 생성하고 중앙 Queue를 균형 배정하는 Corridor 내부 Layout 하위 상태 `MouthMixed`다.

## 잠정 보조 구조: Double Mouth Connector

전방·후방이 각각 Mouth 임계값을 안정적으로 통과한다는 런타임 증거가 확인되면, 새로운 최상위 공간 모드를 바로 추가하기보다 Corridor 내부 하위 상태 `Double Mouth Connector`를 구현한다.

### 진입 조건

1. Player 중심부는 Corridor 폭 조건을 만족한다.
2. 확정 Corridor 축의 전방·후방 횡단면이 모두 유효하다.
3. 두 횡단면이 각각 Mouth 최소 폭과 증가 비율을 만족한다.
4. 위 상태가 초기 시험값 `0.3초` 동안 유지되면 Connector를 확정한다.

### 유지·이탈

- Connector에서는 양쪽 Mouth가 있다는 이유로 즉시 Open 또는 Pocket으로 이탈하지 않는다.
- 한쪽 Mouth만 남은 상태가 초기 시험값 `0.6초` 동안 지속되면 일반 Corridor 또는 단일 Mouth 전환으로 돌아간다.
- Player 주변 자체가 Open 폭을 안정적으로 만족하면 Open으로 전환한다.

### 대형 배치

1. Side 0과 Side 1이 하나의 공통 직선축만 공유하지 않는다.
2. 각 Side는 해당 월드 출구의 정체성과 방향을 독립적으로 고정한다.
3. 단일 Mouth에서는 통로 안쪽에 Corridor Row, 열린 공간 쪽에 Fan 또는 확장 Row를 만든다. 짧은 Double Mouth에서는 두 출구에 각각 확장 Row를 만든다.
4. 양쪽이 유효하면 Attack `3:2`, Wait `4:4`, Holding `8:8`, Pending `floor/ceil` 균형을 기본 목표로 한다. 실제 활성 인원도 Side를 교대 배정해 차이가 최대 `1명`이 되게 한다.
5. 한쪽 Mouth의 NavMesh가 실제로 끊겼을 때만 해당 Side 부족분을 반대쪽으로 Spillover한다.
6. Attack 360도 후보와 Enemy별 완전 경로 검사는 유지한다.

## 단순 Open 처리보다 Connector를 권장하는 이유

이 위치를 Open으로만 처리하면 Player 바로 옆 통로 벽 때문에 원형 Wait·Holding Slot의 다수가 무효가 되거나 한쪽 NavMesh로 투영될 수 있다. 두 출구를 가진 좁은 연결부라는 구조를 보존하면서 양쪽 열린 공간을 각각 별도 전선으로 다루는 편이 의도와 맞다.

## 구현 전 계측 게이트

Connector를 바로 구현하지 않는다. 먼저 다음 정보를 별도로 표시하고 같은 영상을 다시 확인한다.

1. 전방 Mouth 임계값 통과 여부와 실제 폭
2. 후방 Mouth 임계값 통과 여부와 실제 폭
3. `CurrentSpaceMode`, `CandidateSpaceMode`, 전환 누적시간
4. 확정 Corridor 축의 각도 또는 방향 벡터
5. 확정축 부호가 바뀌어도 같은 월드 출구가 같은 Side 정체성을 유지하는지 보여 주는 Side별 Anchor 좌표
6. `RowSides W:A/B H:C/D`
7. Side별 후보 수를 `Row 생성 → Nav 투영 → 직접 경로 검사 → 최종 Layout` 단계로 나눈 값과 각 탈락 사유
8. 계층·Side별 `Generated / Reserved / Arrived` 수

디버그 문자열 자체만으로 결론을 내리지 않고, 쏠림이 보이는 영상 프레임의 실제 Slot 월드 위치와 위 계측값이 같은 시점에 어떻게 변했는지를 함께 대조한다.

같은 위치에서 쏠림 순간마다 `전방 Yes + 후방 Yes`가 유지되는데도 한쪽 Row 후보가 0으로 떨어지면 Connector 가설을 유지한다. 아래의 작은 수정 후에도 재현될 때만 별도 Connector 구조를 확정한다. 반대로 전후 Mouth 판정 또는 확정축 자체가 번갈아 뒤집힌다면 Connector보다 탐침·축 안정화 오류를 먼저 고친다.

Connector 확정 전에는 다음의 더 작은 수정도 같은 계측으로 비교한다.

- 축 부호와 Side의 월드 정체성만 고정
- 기존 Corridor의 양쪽 Row Anchor를 Mouth 중심에 각각 고정
- Nav 투영 실패 후보의 대체 Row 중심 탐색
- 공간 모드는 유지하고 후보 Layout만 별도 안정화

위 수정 중 하나로 양쪽 Layout이 안정되면 새 Connector 상태는 추가하지 않는다.

## Connector 확정 시 구현 범위

1. 전방·후방 Mouth를 각각 판정하는 상태 추가
2. Double Mouth Connector 진입·이탈 히스테리시스 추가
3. Side별 Mouth Anchor와 독립 방향 저장
4. Connector 전용 Wait·Holding·Pending Row 생성
5. `Connector:Yes`, 전후 Mouth 판정, 계층별 Side Slot 수 디버그 추가
6. 영상 위치에서 Player를 좌우로 반복 이동하는 PIE 회귀 검증
7. 긴 단일 Mouth에서 Corridor/Open Side의 실제 `Reserved / Arrived` 인원 차이가 최대 `1명`인지 검증

## 현재 상태

- 전방·후방 Mouth 독립 판정과 `Generated / Reserved / Arrived` 계측을 구현했다.
- 긴 단일 Mouth와 짧은 Double Mouth를 함께 처리하는 Corridor 내부 `MouthMixed`를 구현했다.
- 단일 Mouth는 `Corridor Row + Open Fan`, Double Mouth는 `Fan + Fan`을 사용한다.
- 별도 최상위 Connector 모드는 추가하지 않았다. 같은 MouthMixed 규칙으로 영상 위치를 먼저 PIE 검증한다.
- UE 5.7 `ProjectBHEditor Win64 Development` 빌드에 성공했다.
