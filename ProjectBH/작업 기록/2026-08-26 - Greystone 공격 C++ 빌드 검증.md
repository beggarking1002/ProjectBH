# 2026-08-26 - Greystone 공격 C++ 빌드 검증

## 목적

Greystone 3타 콤보와 다중 검 Trace를 포함한 현재 ProjectBH C++ 변경 사항의 컴파일 상태를 확인한다.

## Codex 변경 사항

- 코드 변경은 하지 않았다.
- `ProjectBHEditor Win64 Development` 전체 빌드를 실행해 현재 공격·콤보·AnimInstance 변경을 검증했다.

## 사용자 에디터 작업

1. `AM_Knight`에 `ANS Melee Hit Window`와 `AN Combo Branch` 배치를 확인한다.
2. 파생 Greystone Mesh에 `Trace_Sword_Base`, `Trace_Sword_Mid`, `Trace_Sword_Tip` Socket을 배치한다.
3. Greystone 플레이어 BP에서 다음 값을 지정·확인한다.
   - `Basic Attack Montage`: `AM_Knight`
   - `Basic Attack Damage Effect`: 기존 기본 공격 피해 Gameplay Effect
   - `Combo Attack Section Names`: `Attack_A`, `Attack_B`, `Attack_C`
   - Trace Socket 이름: `Trace_Sword_Base`, `Trace_Sword_Mid`, `Trace_Sword_Tip`
4. `IA_BasicAttack`이 Boolean이고 `IMC_Default`에서 마우스 왼쪽 버튼에 매핑되며, 입력 설정 데이터 에셋에서 `InputTag.BasicAttack`에 연결됐는지 확인한다.

## 검증

- `ProjectBHEditor Win64 Development` 빌드 결과: `Succeeded`.
- UnrealBuildTool은 Target이 최신 상태임을 확인했다.
- 런타임 Montage 재생·피해·네트워크 결과는 아직 PIE에서 확인하지 않았다.

## 관련 파일

- [[Greystone 3타 콤보 및 다중 스윕 기반 구현]]
- [[Greystone Montage Notify 배치 절차]]
- `/Source/ProjectBH/BHHeroCharacter.cpp`

## 남은 작업 / 다음 단계

- 에디터 에셋 연결을 완료한다.
- Listen Server 1명 + Client 1명 PIE에서 좌클릭 1타·2타·3타, 피해, 중복 적중, 벽 차단을 검증한다.
