# 2026-08-26 - 공용 AnimInstance Direction 계산 추가

## 목적

Greystone 스트레이프 Blend Space에 쓸 Direction을 Blueprint가 아닌 ProjectBH 공용 C++ AnimInstance에서 계산한다.

## Codex 변경 사항

- `UBHCharacterAnimInstance`에 `BlueprintReadOnly` Float `Direction`을 추가했다.
- `NativeThreadSafeUpdateAnimation`에서 캐릭터 회전 기준 Velocity 방향을 계산하도록 구현했다.
  - 전진은 0
  - 좌우는 약 ±90
  - 후진은 약 ±180
- `GroundSpeed`와 `Direction`을 공용 부모 AnimInstance가 계산하므로, `UBHHeroAnimInstance`를 부모로 쓰는 Greystone AnimBP는 두 값을 상속받아 Get으로 연결할 수 있다.
- 이전의 Blueprint Event Graph에서 Get Actor Rotation·Calculate Direction·Set Direction을 직접 만드는 안내를 제거했다.

## 사용자 에디터 작업

1. `ABP_Greystone`의 부모 클래스를 `BH Hero Anim Instance`로 지정한다.
2. Anim Graph의 My Blueprint 변수에서 상속된 GroundSpeed와 Direction을 각각 `Locomotion_BS` 핀에 연결한다.
3. Event Graph에 별도 Direction 변수나 계산 그래프를 만들지 않는다.

## 검증

- 전체 C++ 빌드는 Live Coding을 종료한 상태에서 다시 실행해야 한다.
- PIE에서 전진 0, 좌우 약 ±90, 후진 약 ±180의 Direction 값과 Blend Space 선택을 확인해야 한다.

## 관련 파일

- `/Source/ProjectBH/AnimInstance/BHCharacterAnimInstance.h`
- `/Source/ProjectBH/AnimInstance/BHCharacterAnimInstance.cpp`
- [[Greystone AnimBP Preview Mesh 문제 해결]]

## 남은 작업 / 다음 단계

- Live Coding을 종료한 뒤 전체 C++ 빌드를 통과시킨다.
- Greystone ABP에서 상속 Direction으로 스트레이프 Blend Space를 확인한다.
