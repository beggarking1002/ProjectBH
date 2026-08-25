# 2026-08-26 - Greystone 스트레이프 이동 및 Direction 연결

## 목적

Greystone이 좌우 입력에서 이동 방향으로 돌지 않고 정면을 유지하며 게걸음 애니메이션을 재생하도록 한다.

## Codex 변경 사항

- `ABHHeroCharacter`의 Character Movement를 `bOrientRotationToMovement = false`, `bUseControllerDesiredRotation = true`로 변경했다.
- 이제 캐릭터는 컨트롤러 방향을 향하고, 공용 C++ AnimInstance가 계산한 Direction이 좌우 이동에서 약 ±90이 되어 Blend Space의 스트레이프 샘플을 선택할 수 있다.
- `GroundSpeed`가 이미 공용 `UBHCharacterAnimInstance`에서 계산되는 구조를 확인하고, `Direction`도 같은 클래스에서 C++로 계산하도록 추가했다.
- `ABP_Greystone`는 부모 클래스에서 상속한 `GroundSpeed`, `Direction`을 Get으로 사용하며, Event Graph에서 `Get Actor Rotation`, `Calculate Direction`, `Set Direction` 노드나 별도 Float 변수를 만들 필요가 없도록 변경했다.

## 사용자 에디터 작업

1. `ABP_Greystone` Anim Graph에서 상속된 GroundSpeed와 Direction 변수를 각각 `Locomotion_BS` 핀에 연결한다.
2. Event Graph에 별도 Direction 변수나 계산 그래프를 만들지 않는다.
3. Blend Space에 좌우·후진 애니메이션을 ±90, ±180 위치에 배치한다.
4. Greystone 플레이어 BP가 C++ 회전 설정을 덮어쓰지 않는지 확인한다.

## 검증

- C++ 코드 변경 뒤 전체 빌드는 Live Coding을 종료한 상태에서 다시 실행해야 한다.
- Unreal Editor에서 좌우 이동 시 Direction 값과 스트레이프 Blend Space 샘플 선택을 확인해야 한다.

## 관련 파일

- `/Source/ProjectBH/BHHeroCharacter.cpp`
- [[Greystone AnimBP Preview Mesh 문제 해결]]

## 남은 작업 / 다음 단계

- Live Coding을 종료한 뒤 전체 C++ 빌드를 통과시킨다.
- Greystone ABP에서 스트레이프 이동과 `AM_Knight` Montage 재생을 확인한다.
