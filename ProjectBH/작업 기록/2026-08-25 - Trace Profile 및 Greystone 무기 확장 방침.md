# 2026-08-25 - Trace Profile 및 Greystone 무기 확장 방침

## 목적

근접 공격 Trace Profile의 데이터 구조를 정의하고, 일체형 검·방패를 가진 Greystone을 무기 시스템에 도입하는 순서를 정리했다.

## 결정

- Trace Profile은 공격의 피해가 아니라 추적 Source, Socket/Bone 기준점, Shape, 반지름, 충돌 규칙을 정의한다.
- `DT_TraceProfile`은 `DT_AttackDefinition`의 `TraceProfileId`로 연결한다.
- Combat Component는 `CharacterMesh`와 `EquippedWeaponMesh`를 모두 Trace Source로 지원한다.
- 첫 수직 슬라이스에서는 Greystone의 원래 검·방패를 떼지 않고 `CharacterMesh` Source로 구현한다.
- 플레이어가 Greystone 외형으로 두 번째 무기를 실제 선택할 때에만 파생 메시의 무기 제거 또는 장착형 전환을 결정한다.

## 사용자 에디터 작업

- Greystone Skeletal Mesh 복제본에 `Trace_Sword_Base`, `Trace_Sword_Mid`, `Trace_Sword_Tip`, `Trace_Shield_Center` Mesh Socket을 배치한다.
- 첫 검 공격 Montage에서 해당 소켓이 공격 애니메이션을 따라가는지 확인한다.

## Codex 작업

- 다음 개발 채팅에서 Trace Source enum, Trace Profile DataTable, 서버 스윕 Component를 구현한다.

## 관련 파일

- [[무기 Trace Profile 및 Greystone 확장 방침]]
