# 2026-08-26 - Greystone 검 공격 CombatDummy 피해 연결

## 목표

Greystone 검 3타의 `ANS Melee Hit Window` Sphere Sweep이 서버에서 `BP_BHCombatDummy`의 복제 체력에 실제 피해를 적용하게 한다.

## 기존 확인

- `ABHCombatDummy`는 `ABHBaseCharacter`를 상속하며 `UBHAbilitySystemComponent`, `UBHAttributeSet`을 이미 보유한다.
- 더미의 초기 Health/MaxHealth는 각각 100이고, Health는 서버에서 복제된다.
- `ABHHeroCharacter`는 Pawn 대상 Sweep 후 Target Ability System에 `BasicAttackDamageEffect`를 적용하는 경로를 이미 갖췄다.
- 다만 공격 Effect는 Blueprint에서 별도 지정하지 않으면 비어 있을 수 있었다.

## Codex 구현

- 네이티브 Instant Gameplay Effect `UBHGE_BasicAttackDamage`를 추가했다.
  - 대상 `BHAttributeSet.Health`에 Additive `-20`을 적용한다.
- `ABHHeroCharacter`의 기본 `BasicAttackDamageEffect`를 위 클래스로 지정했다.
  - Greystone BP에서 별도 Effect를 지정하지 않아도 검 한 번 적중 시 더미 Health가 100 → 80으로 감소한다.
  - 이후 Blueprint Gameplay Effect를 지정하면 그 설정이 기본값을 대체한다.
- 서버가 유효 대상에 Effect를 적용하면 Output Log에 `sword hit <ActorName>`을 기록하도록 추가했다.
- `ABHCombatDummy`가 Health Attribute 변경 이벤트를 구독하도록 했다. 서버에서 Health가 감소하면 `CombatDummy '<이름>' took <피해량> damage. Health: <이전> -> <현재>`를 출력한다.

## 사용자 에디터 확인 항목

1. 최신 C++ 빌드 후 Editor를 재시작한다.
2. `AM_Knight`의 각 Attack 구간에 `ANS Melee Hit Window`가 실제 검 접촉 시간만 덮도록 배치됐는지 확인한다.
3. `BP_BHCombatDummy`의 Capsule Collision이 `Query Only` 또는 `Query and Physics`, Object Type이 `Pawn`인지 확인한다.
4. 더미를 칼날이 지나가는 위치에 배치한다.
5. PIE에서 공격 후 Output Log의 `sword hit BP_BHCombatDummy...`와 더미 AttributeSet Health 값의 100 → 80 변화를 확인한다.

## 범위 밖

- 피해 수치 DataTable화
- 체력바 UI
- 피격 애니메이션, 0 체력 사망 처리

## 빌드 검증

- `ProjectBHEditor Win64 Development` 전체 빌드를 실행했다.
- 2026-08-26 결과: `Result: Succeeded`.
- CombatDummy Health 감소 로그 추가 뒤에도 전체 빌드 성공을 확인했다.

## 2026-08-26 런타임 로그 점검

- PIE 로그에서 `BP_BHHeroCharacter... sword hit BP_BHCombatDummy...`가 반복 출력되는 것을 확인했다. 검 Sweep이 Pawn 더미를 찾고 Effect 적용 경로까지 실행되는 상태다.
- 해당 로그에는 새 `CombatDummy ... took ... damage` 줄이 없다.
- 외부 전체 빌드가 DLL을 갱신해도 실행 중인 Unreal Editor는 기존 C++ 모듈을 계속 사용한다. 따라서 Editor를 완전히 종료한 뒤 다시 열어 새 모듈을 로드해야 CombatDummy Health 변경 로그가 실행된다.
- 재시작 후에도 체력 감소 로그가 없으면 `BP_BHHeroCharacter` Class Defaults의 `Basic Attack Damage Effect`가 네이티브 `BHGE_BasicAttackDamage`가 아닌 기존 Effect로 덮어써졌는지 확인한다. 현재 `sword hit`이 출력되므로 해당 Effect는 비어 있지 않다.
