# Wait에서 Attack 돌진 공격 작업 취소

## 결정

Wait Slot에서 Attack Slot으로 승격할 때 돌진 공격을 사용하는 기능은 사용할 애니메이션이 없어 취소했다.

## 롤백 범위

- `Charging` 전투 상태와 돌진 공격 실행 코드 제거
- Enemy Config의 `WaitToAttackChargeId` 제거
- 돌진 전용 이동속도와 Wait→Attack 승격 알림 확장 제거
- 몬스터 이동 시스템 규칙 문서에서 돌진 관련 현행 규칙 제거
- 기존 Wait→Attack 연속 이동, Ring 정렬, Attack Slot 도착 후 기본 공격은 유지

## 검증

- 돌진 관련 tracked 코드와 기획 문서가 이번 돌진 기능 작업 착수 직전 상태와 일치함을 Git diff로 확인했다.
- Unreal Engine 5.7 `ProjectBHEditor Win64 Development` 빌드 성공.
- `/Game/Enemy/AM_Guardian_rush` 에셋은 사용자가 만든 에디터 에셋이므로 이번 코드 롤백에서 삭제하지 않았다.
