# Enemy 중첩 교착 watchdog 보완

## 증상

- Enemy 둘이 겹치면 서로 밀거나 떨기만 하고 각자의 Slot으로 이동하지 못한다.
- 그중 Attack Slot 예약자가 외곽에 남아 있어도 예약이 유지되어 다른 Enemy가 해당 Attack Slot을 사용할 수 없다.
- Player가 크게 움직여 Formation 목적지가 갱신되면 교착이 풀린다.

## 원인

기존 watchdog은 목표까지 `20 cm` 이상 가까워지지 못한 상태에서 실제 속도가 `10 cm/s` 미만일 때만 시간을 누적했다. Capsule 충돌과 Detour Crowd 조향으로 두 Enemy가 제자리에서 서로 밀면 실제 목적지에는 가까워지지 않지만 속도는 `10 cm/s`를 넘을 수 있다. 이때 기존 교착 시간은 매 갱신마다 0으로 초기화되어 복구가 영구히 실행되지 않았다.

Player가 크게 움직일 때 풀린 것은 Formation Revision과 Move Goal이 바뀌면서 서로 다른 경로가 다시 요청되었기 때문이다. 겹침이 해결된 것이 아니라 목적지 변경이 우연히 대칭 교착을 깨뜨린 상태였다.

## 변경

- 기존 저속 watchdog `2초`를 유지한다.
- 첫 표본의 Move Goal 잔여 거리를 진행 기준으로 저장한다. 이후 기본 `0.5초` 갱신마다 `기준 거리 - 현재 거리`가 누적 `20 cm` 이상이면 기준을 현재 거리로 바꾸고 두 watchdog을 초기화한다. 매 표본마다 `20 cm`를 전진해야 하는 규칙은 아니다.
- 누적 `20 cm` 진행에 도달하지 못한 동안에는 속도와 무관한 미진행 시간을 별도로 누적한다.
- 강제 미진행 시간이 `4초`에 도달하면 `Stalled` 복구를 실행한다.
- 저속 `2초` 또는 미진행 `4초` 중 먼저 충족한 한 조건이 같은 `Stalled` 복구를 한 번 실행한다.
- Attack 예약자는 기존 규칙대로 도착한 Wait 후보와 교대를 우선한다.
- 교대 후보가 없거나 Wait·Holding·Pending 예약자라면 현재 Slot을 반납한다. 방금 실패한 Slot은 해당 Enemy에게만 `2초` 동안 제외되며, 다음 갱신에서 다른 Slot을 우선 요청한다.
- 최초 Move 요청, Route Stage 변경 또는 현재 Slot 계층의 재경로 거리 이상으로 Move Goal이 바뀌면 이전 이동 구간의 진행 표본을 초기화한다. 기본 재경로 거리는 Attack `80 cm`, Wait `150 cm`, Holding `250 cm`다. Formation Revision으로 예약 배치가 갱신되는 경우에도 Controller의 진행 표본을 초기화한다.
- Crowd 디버그를 초 단위 `Stuck:저속/미진행` 형식으로 확장한다. `Last:Stalled`은 복구가 실행되었다는 뜻이며 최종 분리 성공 자체를 보장하는 표시는 아니다.

## PIE 확인

1. `bh.Debug.Enabled 1`, `bh.Debug.Crowd 1`, `bh.Debug.Slots 1`을 켠다.
2. Enemy 둘이 정면 또는 비스듬히 겹쳐 서로 밀게 만든다.
3. Player 입력을 멈추고 최소 `5초` 동안 화면을 녹화한다. Player 이동으로 Formation이 바뀌어 우연히 풀리는 결과를 섞지 않는다.
4. `Speed`가 높더라도 `Stuck`의 두 번째 수치가 증가하는지 확인한다.
5. 진행 기준을 잡은 다음 목표에 누적 `20 cm` 이상 가까워지지 못하면 `4초 + 갱신 주기 1회` 안에 `Last:Stalled`이 표시되는지 확인한다.
6. 외곽의 Attack 예약자는 Wait로 교대되거나, 후보가 없으면 예약을 반납하는지 확인한다.
7. 최종 판정은 디버그 문자열 자체가 아니라 실제 두 Capsule이 분리되고 외곽 Attack 예약자가 새 목적지로 이동하는지로 한다. 디버그 값은 원인 확인용으로만 사용한다.
8. 대조군으로 벽을 정상 우회하는 Enemy도 `5초` 이상 관찰해, 이동 구간 변경 또는 `20 cm` 진행 때 미진행 시간이 초기화되고 `Stalled`가 발생하지 않는지 확인한다.

## 합격 기준

- Player가 움직이지 않아도 미진행 시작 후 `4초 + 갱신 주기 1회` 안에 교대 또는 Slot 반납 복구가 시작된다.
- 외곽에서 막힌 Enemy가 Attack Slot을 영구 점유하지 않는다. 첫 재배정도 같은 병목에 막히면 watchdog이 다시 동작하며, 한 번의 복구가 물리적 분리를 보장하지는 않는다.
- 정상 우회 이동은 목표 진행 또는 이동 구간 변경으로 watchdog이 초기화된다.

## 2차 확인: Recovering Attack 점유자

### 스크린샷 판독

추가 스크린샷의 뒤쪽 Enemy는 `Recovering`, `Attack[0]`, `Speed:0`, `Route:Direct`, `Stuck:0/0` 상태였다. 빨간 선은 현재 Attack Slot과 해당 Enemy가 떨어져 있음을 보여준다. 이 Enemy는 일반 이동 코드와 watchdog에 도달하기 전에 `IsAttackLocked()` 분기에서 `StopMovement()`되므로, 미진행 watchdog을 추가한 것만으로는 복구되지 않는다.

### 추가 변경

- `Attacking` 중에는 공격 애니메이션을 보존하기 위해 기존처럼 이동을 고정한다.
- `Recovering`에 들어가면 각 Controller 갱신에서 Enemy `ActorLocation`과 현재 Player를 따라 갱신된 Attack Slot 위치의 XY 거리를 다시 계산한다.
- 적용 중인 `Recovering Attack Slot Leash Distance`보다 거리가 클 때 일반 watchdog을 기다리지 않고 `Stalled` 복구를 실행한다. 기본값은 `60 cm`이며 정확히 같은 거리는 허용한다.
- 여기서 `Stalled`는 Combat State가 아니라 Slot 복구 사유다. Enemy의 Combat State는 `Recovering`으로 유지된다.
- 도착한 Wait 후보가 있으면 기존 20.3절 규칙으로 한 서버 처리 안에서 Wait 후보와 기존 Attack 점유자의 예약을 원자적으로 교환한다. 강등 Enemy는 후보가 비운 Wait Slot을 받고 Attack 재승격이 `2초` 제한된다.
- 후보가 없으면 Attack Slot을 반납하고 기존 Queue Sequence를 유지한다. Recovery 종료 후 다시 예약을 요청하며, 방금 실패한 Attack Slot만 해당 Enemy에게 `2초` 제외된다.
- 강등된 Enemy는 남은 Recovery 동안 정지한 뒤 `Chasing`으로 돌아오면 새 Wait Slot으로 이동한다.

### 추가 PIE 합격 기준

- 스크린샷과 같은 `Recovering + Attack` Enemy가 실제 적용 Leash보다 멀다면 다음 개별 Controller 갱신에서 `Last:Stalled`과 함께 Wait로 교대하거나 예약을 반납한다. 화면 첫 번째 시간값이 해당 Enemy의 갱신 주기다.
- Wait 후보가 존재하는 경우 다음 후보 Controller 갱신에서 Attack 예약과 MoveTo 요청이 확인되어야 한다. 실제 도착은 Nav 경로와 추가 교착의 영향을 받으므로 이 단계의 즉시 보장으로 삼지 않는다.
- 뒤쪽 Enemy는 교대되었다면 Recovery 종료 후 후보가 비운 Wait 목적지로 이동한다. 후보가 없어 반납했다면 새 예약 결과를 따른다.
- 권위 상태가 `Attacking`인 Enemy는 이번 Recovering 이탈 복구 때문에 이동하거나 Slot을 반납하지 않는다. 사망·경직·타깃 무효화 같은 기존 상위 취소 규칙은 그대로 적용한다.
