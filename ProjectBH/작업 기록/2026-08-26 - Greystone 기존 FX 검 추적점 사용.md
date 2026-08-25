# 2026-08-26 - Greystone 기존 FX 검 추적점 사용

## 목적

Greystone Skeleton의 기존 `FX_Sword_Bottom`, `FX_Sword_Top` 본/Socket을 검 타격 추적점으로 사용해, 불필요한 새 Mesh Socket 생성을 줄인다.

## Codex 변경 사항

- `ABHHeroCharacter`의 기본 검 Trace 시작·끝 이름을 `FX_Sword_Bottom`, `FX_Sword_Top`으로 변경했다.
- Mid Trace 이름을 선택 사항으로 변경했다. 지정하지 않으면 C++가 Base와 Tip의 중간 위치를 자동 보간해 세 지점 다중 Sphere Sweep을 유지한다.
- Base 또는 Tip이 유효하지 않을 때만 공격을 중단하고 오류를 기록하도록 검증 조건을 완화했다.
- 에디터 연결 가이드를 기존 FX 추적점 우선 사용 방식으로 갱신했다.

## 사용자 에디터 작업

1. Preview Scene에서 `FX_Sword_Bottom`, `FX_Sword_Top`이 공격 중 실제 검의 양 끝을 따라가는지 확인한다.
2. 맞으면 Greystone BP의 Sword Trace Base/Tip Name에 각각 두 이름을 지정하고 Mid Name은 비워 둔다.
3. 맞지 않을 때만 파생 Mesh에 별도 Mesh Socket을 만들어 해당 이름으로 교체한다.

## 검증

- 기존 추적점이 실제 검 끝을 따라가는지는 Unreal Editor Preview에서 확인해야 한다.
- C++ 변경 뒤 전체 빌드는 Live Coding을 종료한 상태에서 다시 실행해야 한다.

## 관련 파일

- `/Source/ProjectBH/BHHeroCharacter.h`
- `/Source/ProjectBH/BHHeroCharacter.cpp`
- [[Greystone 3타 콤보 에디터 연결 절차]]

## 남은 작업 / 다음 단계

- 두 FX 추적점의 위치를 확인한다.
- 유효하면 새 Socket 없이 Montage·PIE 공격 검증을 진행한다.
