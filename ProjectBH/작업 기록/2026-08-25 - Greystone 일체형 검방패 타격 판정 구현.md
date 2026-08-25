# 2026-08-25 - Greystone 일체형 검방패 타격 판정 구현

> 상태 변경: 공격 판정 방식 검토 전의 **후보 구현**이다. 사용자가 방식을 선택하기 전까지 `SwordTraceTip` 생성 및 Montage 설정을 진행하지 않는다.

## 목적

Greystone의 일체형 검+방패 주무기 세트로 서버 권한 근접 타격과 피해를 적용할 기반을 구현한다. 보조 총기 전환은 이번 범위에서 제외한다.

## Codex 변경 사항

- Greystone의 확인 경로를 `/Game/ParagonGreystone/Characters/Heroes/Greystone/Meshes`로 기록했다.
- 검과 방패가 본체 Skeletal Mesh에 일체형이며, Skeleton 본 이름이 각각 `sword`, `shield`임을 기획 문서와 작업 순서에 반영했다.
- `ABHHeroCharacter`에서 과거 외부 도끼를 Spawn하던 `StartingWeaponClass`, `StartingWeaponSocketName`, `EquippedWeapon`, `SpawnStartingWeapon` 흐름을 제거했다. 게임 시작 시 Greystone은 본체에 포함된 검+방패 주무기 세트를 그대로 사용한다.
- 새 `UBHAnimNotifyState_MeleeTrace` (`BH Melee Trace`)를 후보 구현으로 추가했다.
- `BH Melee Trace`가 활성화된 동안 서버는 `sword` 본과 `SwordTraceTip` 소켓 사이를 여러 점으로 나눠 이전 프레임에서 현재 프레임까지 Sphere Sweep한다.
- 한 공격 활성 구간에 같은 대상에게 피해가 중복 적용되지 않도록 처리하고, 기존 `BasicAttackDamageEffect`로 GAS 피해를 적용한다.
- 기존 `BH Basic Attack Hit` 단일 Notify는 기존 에셋 호환을 위해 남겼다. Greystone 공격 Montage에는 새 `BH Melee Trace`를 사용한다.

## 사용자 에디터 작업

1. Greystone Skeletal Mesh의 `sword` 본에 **Mesh Socket** `SwordTraceTip`을 만든다.
2. 소켓을 칼날 끝에 배치하고, Preview Scene에서 공격 애니메이션 중에도 칼날 끝을 따르는지 확인한다.
3. Greystone 검 공격 애니메이션을 ProjectBH 전용 Montage로 복제한다.
4. 타격이 유효한 구간 전체에 `Add Notify State → BH Melee Trace`를 배치한다. 기존 `BH Basic Attack Hit`은 이 Montage에 추가하지 않는다.
5. Greystone 기반 플레이어 BP에 새 Montage와 기존 피해 Gameplay Effect를 `Basic Attack Montage`, `Basic Attack Damage Effect`로 지정한다.
6. C++ 모듈을 다시 컴파일한 뒤 Listen Server 1명 + Client 1명 PIE에서 검 타격과 더미 체력 감소를 확인한다.

## 검증

- UnrealHeaderTool은 새 Notify State를 포함한 헤더 5개를 성공적으로 처리했다.
- 전체 `ProjectBHEditor Win64 Development` 빌드는 Unreal Editor의 Live Coding 활성화로 중단됐다. 오류 메시지는 `Unable to build while Live Coding is active`이며, C++ 컴파일 단계까지 진행되지는 않았다.
- 사용자에게 Unreal Editor와 게임을 종료하거나 Live Coding을 종료한 뒤 전체 빌드를 다시 실행해야 한다.

## 관련 파일

- `/Source/ProjectBH/BHHeroCharacter.h`
- `/Source/ProjectBH/BHHeroCharacter.cpp`
- `/Source/ProjectBH/Animation/BHAnimNotifyState_MeleeTrace.h`
- `/Source/ProjectBH/Animation/BHAnimNotifyState_MeleeTrace.cpp`
- [[Paragon 기반 캐릭터 구현 계획]]
- [[작업 순서]]

## 남은 작업 / 다음 단계

- `SwordTraceTip` Mesh Socket과 Greystone 공격 Montage의 `BH Melee Trace`를 설정한다.
- Live Coding을 종료한 뒤 전체 빌드를 재실행한다.
- 2인 PIE에서 서버 권한 피해가 정상 복제되는지 검증한다.
