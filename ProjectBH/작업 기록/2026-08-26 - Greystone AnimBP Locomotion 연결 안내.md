# 2026-08-26 - Greystone AnimBP Locomotion 연결 안내

## 목적

Greystone 전용 Animation Blueprint의 Anim Graph에서 Locomotion Blend Space와 Output Animation Pose를 연결하는 방법을 안내한다.

## Codex 변경 사항

- `Locomotion_BS`의 `None` 입력은 Blend Space 축 이름이 비어 있어 표시되는 것임을 설명했다.
- 비어 있는 축을 `Ground Speed`로 이름 붙이고, Ground Speed·Direction 변수와 Blend Space 포즈 출력을 연결하는 최소 그래프를 개발 가이드에 추가했다.
- `AM_Knight` Montage 재생을 위해 Locomotion Blend Space와 Output Pose 사이에 같은 Slot Name의 Slot 노드를 넣어야 함을 기록했다.

## 사용자 에디터 작업

1. `Locomotion_BS`의 Axis Settings에서 `None` 축 이름을 `Ground Speed`로 바꾼다.
2. `Ground Speed` 변수를 해당 핀에 연결한다.
3. 초기에는 `Direction`을 0으로 두고, Blend Space 출력 포즈를 `Slot(DefaultSlot)`을 거쳐 Output Animation Pose에 연결한다.

## 검증

- 제공된 Anim Graph 스크린샷에서 Blend Space의 두 번째 축이 `None`으로 표시되는 것을 기준으로 절차를 작성했다.
- Unreal Editor 내부 연결은 사용자가 수행해야 한다.

## 관련 파일

- [[Greystone AnimBP Preview Mesh 문제 해결]]

## 남은 작업 / 다음 단계

- Greystone Mesh에서 Idle·이동이 재생되는지 확인한다.
- `AM_Knight` Montage와 같은 Slot을 연결하고 3타 콤보 재생을 확인한다.
