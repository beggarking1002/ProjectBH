# Corridor Lane Queue 규칙 구현

## 목표

좁은 통로 선형 대형을 단순한 Slot 모양이 아니라 실제 줄서기 규칙으로 완성한다. 같은 Lane의 후발 Enemy가 선두를 건너뛰어 Attack으로 승격하거나, 빈자리를 찾아 옆 Lane으로 이동하며 교착을 만드는 것을 막는다.

## 구현 위치

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.h`
- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.cpp`

## 구현 내용

### Lane 고정

- Corridor Lane 수는 1~2다.
- Lane은 `(Queue Sequence - 1) % Lane Count`로 결정한다.
- Queue Sequence를 보존하는 승격·강등·교착 복구에서는 Lane도 유지된다.
- Wait·Holding·Pending의 Slot Index는 `Row * Lane Count + Lane`이다.
- Enemy 디버그 문자열에 `Lane`을 추가했다. Open에서는 `-1`, Corridor에서는 `0~1`을 표시한다.

### Lane별 압축

- Wait와 Holding을 Queue Sequence 오름차순으로 정렬한다.
- 각 Enemy는 자기 Lane의 첫 번째 빈 Row에만 들어간다.
- 중간 예약이 빠지면 다른 Lane은 건드리지 않고 같은 Lane 뒤쪽만 앞으로 당긴다.
- Wait의 자기 Lane 수용량이 가득 차면 Holding, Holding도 가득 차면 Pending으로 남는다.

### Attack 승격

- 빈 Attack Slot과 같은 Lane의 Wait 선두를 먼저 검사한다.
- 선두가 Slot에 도착했고 재진입 Cooldown이 끝났으며 Attack Slot까지 NavMesh 경로가 있어야 한다.
- 같은 Lane 선두가 준비되지 않았을 때만 다른 Lane 선두를 보조 후보로 사용한다.
- Corridor에서는 후방 Wait 전체를 경로 비용으로 비교하지 않는다.
- 초기 편성도 각 Lane의 가장 오래된 미예약 Enemy만 Attack 후보로 사용한다.

### Holding·Pending 승격

- Wait의 빈 Row는 같은 Lane Holding 선두가 채운다.
- Holding의 빈 Row는 같은 Lane Pending 선두가 채운다.
- 목적지 거리가 가깝다는 이유로 다른 Lane 후발 Enemy가 건너오지 않는다.

### 반납과 복구

- 사망·경직·교착·경로 실패로 예약이 반납되면 Wait와 Holding을 즉시 Lane별로 다시 압축한다.
- 유효하지 않은 Actor를 Prune한 경우에도 같은 규칙을 적용한다.
- Attack 교착 교대는 같은 Lane Wait 선두를 우선한다.
- 공격·회복 잠금 수가 공간상 목표 Attack 수보다 많으면 잠금을 유지하고, 종료된 수만큼 순차적으로 1~2 Attack 기준으로 축소한다.

### 2단계 기준 수정

선형 대형 2단계에서 임시로 허용했던 최대 3 Lane과 4 Attack을 원래 계획에 맞게 수정했다.

| 항목 | 현행 |
| --- | ---: |
| Corridor Lane | 1~2 |
| 정상 Attack | Lane당 1명, 총 1~2명 |
| 임시 초과 Attack | 공간 전환 당시 공격·회복 잠금 수만큼, 잠금 종료 후 축소 |

## 검증

- UE 5.7 `ProjectBHEditor Win64 Development` 빌드 성공
- UHT와 C++ 컴파일 성공
- 실제 Detour Crowd 이동에서 물리적 추월·끼임이 얼마나 줄어드는지는 PIE 검증이 필요하다.

## 사용자 PIE 확인 순서

1. 2 Lane으로 판정되는 통로에 8~12마리를 배치하고 `bh.Debug.Slots 1`, `bh.Debug.Crowd 1`을 켠다.
2. 각 Enemy의 `Lane`, `Seq`, Slot Index를 확인한다.
3. 같은 Lane에서는 Seq가 작은 Enemy가 항상 앞 Row에 있는지 확인한다.
4. Attack 한 명을 죽여 같은 Lane Wait 선두가 먼저 승격하는지 확인한다.
5. 해당 Lane 선두를 일부러 먼 곳에 두고, 뒤 Row가 선두를 건너뛰어 승격하지 않는지 확인한다.
6. 같은 Lane 후보가 없을 때만 다른 Lane 선두가 빈 Attack을 채우는지 확인한다.
7. Wait 선두가 승격하면 같은 Lane Wait와 Holding만 한 Row씩 연속 전진하는지 확인한다.
8. 선두를 경직·사망·교착시키고 예약 반납 뒤 중간 빈 Row가 남지 않는지 확인한다.
9. Open으로 나오면 Lane이 `-1`이 되고 기존 원형 Admission 규칙으로 복귀하는지 확인한다.

## 해석 주의

현행 추월 금지는 Queue 우선순위와 목적지 순서에 대한 금지다. Detour Crowd가 충돌 회피를 위해 순간적으로 옆으로 비키는 이동까지 막으면 오히려 통로가 막히므로, 물리 이동의 좌우 회피는 허용한다.
