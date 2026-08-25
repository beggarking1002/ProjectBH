# 2026-08-25 - Greystone 검방패 구현 계획 및 코드 영향 분석

## 목적

Greystone 검+방패 수직 슬라이스와 이후 보조 총기 전환을 구현할 작업 순서를 정하고, 현재 C++ 기반에서 변경이 필요한 부분을 확인한다.

## Codex 변경 사항

- `작업 순서`를 Greystone 중심의 0~5단계 구현 계획으로 갱신했다.
- 첫 단계에 Greystone 에셋 샌드박스와 일체형 근접 무기의 실제 숨김 본·장착 소켓 검사 절차를 넣었다.
- 현재 단일 외부 도끼 장착 구조를 내장 근접 무기, 방패, 보조 총기 슬롯 및 복제 장비 모드 구조로 바꿔야 함을 정리했다.
- 단일 구형 Sweep과 순간 Anim Notify를 검 시작/끝 소켓 기반의 연속 Sweep 및 Anim Notify State로 교체해야 함을 정리했다.
- 입력 태그, 전투 상태, 사망 처리의 현재 공백을 구현 순서에 반영했다.

## 사용자 에디터 작업

이번 작업에서는 에셋 또는 블루프린트를 변경하지 않았다. 다음 단계에서 사용자는 Greystone 에셋 샌드박스를 수행하고, 검·방패 장착 본/소켓과 근접 무기 숨김 본의 실제 이름을 전달한다.

## 검증

- `ABHHeroCharacter`, `ABHWeapon`, `UBHAttributeSet`, `UBHAnimNotify_BasicAttackHit`의 현재 구현을 검토했다.
- 현재 구현이 외부 도끼 하나, `InputTag.BasicAttack`, 캐릭터 중심 구형 Sweep, 즉시 Anim Notify, 단일 쿨다운을 전제로 함을 확인했다.
- C++ 및 Unreal 에셋은 변경하지 않았으므로 빌드는 실행하지 않았다.

## 관련 파일

- [[작업 순서]]
- `/Source/ProjectBH/BHHeroCharacter.h`
- `/Source/ProjectBH/BHHeroCharacter.cpp`
- `/Source/ProjectBH/Weapons/BHWeapon.h`
- `/Source/ProjectBH/Weapons/BHWeapon.cpp`
- `/Source/ProjectBH/Animation/BHAnimNotify_BasicAttackHit.h`
- `/Source/ProjectBH/Animation/BHAnimNotify_BasicAttackHit.cpp`

## 남은 작업 / 다음 단계

- 사용자가 Greystone을 추가하고 에셋 샌드박스 결과를 공유한다.
- 결과에 따라 장비 슬롯·복제 장비 모드·검 궤적 Trace를 C++로 구현한다.
- 검+방패 기본 공격의 2인 PIE 검증 뒤 가드와 보조 총기 전환을 구현한다.
