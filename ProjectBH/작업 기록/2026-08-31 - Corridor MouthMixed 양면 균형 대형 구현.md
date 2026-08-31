# Corridor MouthMixed 양면 균형 대형 구현

## 목적

Player가 Corridor Mouth 앞에 있을 때 Enemy가 열린 공간 한쪽에만 몰리지 않게 한다. 긴 단일 Mouth에서는 통로 안쪽과 열린 공간 쪽을 함께 채우고, 짧은 Double Mouth에서는 두 출구를 함께 채운다.

## 구현 파일

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`

## 구현 규칙

1. 전방·후방 횡단면을 각각 `Raw / Held / No`로 판정한다.
2. Player 주변 또는 반대 표본에 좁은 단면이 남아 있을 때만 넓은 횡단면을 Mouth로 인정한다. 모든 표본이 넓은 일반 Open은 Mouth가 아니다.
3. 같은 방향 Mouth `Raw` 증거가 `0.3초` 연속 유지되면 최상위 공간 모드를 바꾸지 않고 Corridor 내부 `MouthMixed`에 진입한다. `Held`는 진입이 아니라 이탈 유예에만 쓴다.
4. MouthMixed 동안 다른 종류의 Raw가 `0.6초` 연속 유지될 때만 Side0·Side1·Double 종류를 전환해 작은 Player 이동으로 대형이 반전되지 않게 한다.
5. 단일 Mouth의 열린 Side는 Engagement Anchor 중심의 최대 반각 `80도` Fan Row를 사용하고, 반대 Side는 Corridor 동적 Row를 유지한다.
6. Double Mouth는 양쪽 Side 모두 Fan Row를 사용한다.
7. 기존 양측 후보 수집과 교대 Slot 배열을 재사용해 Wait `4:4`, Holding `8:8`, Pending `floor/ceil`을 우선한다.
8. Attack과 Wait·Holding에서 한쪽 후보가 부족한 토폴로지는 각 확정 대기시간 동안 유지된 뒤에만 Spillover한다. Pending은 예약 Layout이 없으므로 현재 Queue Prefix를 중앙부터 양 Side로 교대 생성한다.
9. Attack은 기존 360도 `16개` 후보 검사를 유지하되 MouthMixed에서는 `활성 수 → Side 균형 → 축 정렬 → 기존 Sample 유지` 순으로 조합을 골라 양쪽이 유효할 때 `3:2`를 우선한다.
10. Player의 NavMesh 투영이 한 번 실패해도 `0.6초` 동안 마지막 확정 대형을 유지한다.

## 디버그

- 전방·후방 횡단면 색은 서로 독립이다.
  - 하늘색: Mouth 아님
  - 빨간색: Raw
  - 주황색: Held
- Corridor 방향 화살표는 보라색, Mouth Fan 방향 화살표는 주황색이다.
- 첫 번째 문자열은 `Mouth F`, `Mouth R`, 현재 `Mixed`, 전환 후보 `Next`, 누적시간과 폭을 표시한다.
- 두 번째 문자열은 Side별 Wait·Holding `Layout`, 실제 예약 `Assigned`, 예약 전 Queue 목표 `TargetQ`, 목표 반경 도착 수 `NearTarget`을 표시한다.

## 빌드

- UE 5.7 `ProjectBHEditor Win64 Development`: 성공

## 재검토 보정

- 활성 Mouth 종류를 영구 고정하던 초기 구현을 제거했다. 반대 종류의 Raw가 `0.6초` 연속될 때만 Side0·Side1·Double 사이를 전환한다.
- 한 번의 Raw 뒤 Held만 남아도 진입 시간이 채워지던 문제를 막았다. MouthMixed 신규 진입은 Raw만 누적한다.
- Player NavMesh 투영이 한 표본 실패했을 때 확정 대형 전체가 즉시 해제되던 문제를 `0.6초` 유예로 막았다.
- 디버그 문자열의 의미를 실제 집계와 맞춰 `Layout / Assigned / TargetQ / NearTarget`으로 고쳤다.
- Double Mouth의 두 Fan은 물리적 횡단면 중심을 새 원점으로 쓰는 것이 아니라, 확정된 양쪽 출구 방향과 공통 Engagement Anchor를 사용한다고 문서화했다.

## PIE 검증 절차

1. `bh.Debug.Slots 1`을 켠다.
2. 긴 Corridor의 단일 Mouth 안팎으로 천천히 이동한다.
3. `Mixed:Side0Open` 또는 `Side1Open`이 Raw 연속 판정 약 `0.3초` 뒤 유지되는지 확인한다.
4. Fan Side 화살표가 주황색, Corridor Side 화살표가 보라색인지 확인한다.
5. Enemy 12마리 이상에서 `Layout W:4/4 H:8/8`과 `Assigned / NearTarget`이 양쪽에 형성되는지 진단한다.
6. 최종 합격은 위 내부 카운터만으로 판정하지 않는다. 녹화 화면에서 실제 Enemy Actor를 Corridor 축의 양쪽으로 직접 세거나 별도 Actor Transform 기록으로 재분류해 Side 인원 차이가 최대 `1명`인지 대조한다.
7. 짧은 양방향 통로에서 `Mixed:Double`과 양쪽 주황색 화살표를 확인한다.
8. Player가 조금 움직여도 Side 전체가 반대쪽으로 즉시 뒤집히지 않는지 확인한다.
9. Player가 Mouth에서 충분히 벗어나면 MouthMixed가 해제되고 일반 Corridor 또는 Open 대형으로 돌아가는지 확인한다.

## 남은 검증

- `NearTarget`은 현재 `PromotionArrivalRadius`를 공통 진단 반경으로 사용한다. 실제 Attack 개시 허용오차와는 별도 값이다.
- 굽은 Mouth와 NavMesh가 부분적으로 끊긴 Mouth에서 Fan 후보 투영 및 Spillover를 추가 검증해야 한다.
- Pending은 예약 전 Queue라서 Wait·Holding과 같은 커밋 지연 Layout을 갖지 않는다. 실제 영상에서 Pending 목표가 한쪽으로 반복해서 튀는지 별도로 확인한다.
