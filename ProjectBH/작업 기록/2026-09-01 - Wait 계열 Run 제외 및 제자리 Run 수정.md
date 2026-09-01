# Wait 계열 Run 제외 및 제자리 Run 수정

## 입력 자료

- `bandicam 2026-09-01 18-08-22-937.mp4`

## 관찰

밀집 대형의 외곽과 전열 사이에서 일부 Enemy가 목표 Slot 선을 유지한 채 월드 위치는 거의 변하지 않지만 Run cycle을 계속 재생한다. 기존 Debug의 `Speed`는 실제 위치 변화가 아니라 CharacterMovement Velocity이므로, Detour Crowd가 이동을 시도하지만 군중 회피로 순진행하지 못하는 경우에도 0이 아닐 수 있다.

기존 no-progress watchdog도 이 상황을 감지하지만 기본 4초가 지나야 Slot 복구를 실행하므로 Run을 추가한 뒤에는 대기 시간이 시각적으로 크게 드러났다.

## 수정 규칙

### Run 허용 Slot

- 포메이션 밖 직접 추격: Run 허용
- 초기 Attack 후보와 Attack Slot 진입/Catch-up: Run 허용
- Wait, Holding, Pending: Run 금지, 기존 `Guardian_BS` 사용
- 공격, 회복, 피격, 사망: Run 금지

AI Controller가 복제되는 `bWantsRunLocomotion`을 명시적으로 설정하므로 AnimInstance가 과거의 합류 상태만으로 Slot 의도를 추측하지 않는다.

### 실제 이동 기준 애니메이션

Enemy의 `GroundSpeed`를 프레임 간 실제 월드 위치 변화로 다시 계산한다.

- 실제 이동속도는 `ObservedGroundSpeed`에 저장한다.
- 보간 속도 기본값은 `12`다.
- Pool 이동이나 Teleport처럼 한 프레임에 1000cm를 넘는 변화는 locomotion 속도로 취급하지 않는다.
- Detour Velocity가 남아 있어도 실제 위치 변화가 없으면 Run/Jog가 Idle로 복귀한다.

### Run 교착 복구

- 일반 no-progress watchdog: 기존 4초 유지
- Run 의도 중 no-progress watchdog: 1.5초
- Attack Slot 진입 Run이 1.5초 동안 목표 거리 20cm 이상을 줄이지 못하면 기존 stalled Attack 처리로 빠르게 넘긴다.

## 변경 파일

- `Source/ProjectBH/Enemies/BHEnemy.h/.cpp`
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.h/.cpp`
- `Source/ProjectBH/AnimInstance/BHEnemyAnimInstance.h/.cpp`
- [[상하체 애니메이션 분리 및 Enemy Run 연결 절차]]

## 에디터 작업

ABP 수정은 없다. 기존 `bShouldUseRunLocomotion` 연결을 유지한다.

## 검증 상태

- `ProjectBHEditor Win64 Development` UHT, 컴파일, 링크 성공.
- PIE에서 Wait/Holding/Pending의 `Gait:Formation`, Attack 접근의 `Gait:ApproachRun/CatchUpRun`을 확인해야 한다.
- 막힌 Enemy가 실제 위치 변화 없이 Run을 계속 재생하지 않고 Idle로 내려오는지 확인해야 한다.
