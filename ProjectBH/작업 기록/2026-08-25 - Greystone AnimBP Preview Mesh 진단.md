# 2026-08-25 - Greystone AnimBP Preview Mesh 진단

## 목적

기존 `ABP_Hero`의 Preview Mesh 목록에 Greystone이 보이지 않는 원인을 설명하고, Greystone 전용 Animation Blueprint 운용 방침을 정리한다.

## Codex 변경 사항

- Animation Blueprint가 Target Skeleton에 묶여 있으며, 다른 Skeleton의 Greystone이 기존 `ABP_Hero` Preview Mesh 목록에 보이지 않는 것은 정상적인 호환성 제약임을 정리했다.
- 첫 전투 수직 슬라이스에는 Greystone Skeleton을 대상으로 `ABP_Greystone`을 새로 만들고, 기존 `ABP_Hero`를 강제로 재사용하지 않도록 권장했다.
- Montage 공격 재생에 필요한 Anim Graph Slot 노드와 `AM_Knight` Slot Name 일치 조건을 기록했다.

## 사용자 에디터 작업

1. `ABP_Hero`와 Greystone Skeletal Mesh의 Skeleton 경로를 비교한다.
2. 경로가 다르면 Greystone Skeleton용 `ABP_Greystone`을 만든다.
3. `ABP_Greystone`에 `AM_Knight`와 같은 Slot을 연결하고 Greystone 플레이어 BP에 지정한다.

## 검증

- 현재 파일 시스템만으로 두 `.uasset`의 내부 Skeleton 참조를 확정할 수 없으므로, Unreal Editor의 Asset Details에서 확인해야 한다.
- Animation Blueprint의 Skeleton별 호환 규칙을 기준으로 진단했다.

## 관련 파일

- [[Greystone AnimBP Preview Mesh 문제 해결]]
- `/Content/PlayerCharacter/AnimBP/ABP_Hero.uasset`

## 남은 작업 / 다음 단계

- 두 Skeleton 경로 비교 결과를 공유한다.
- Greystone 전용 AnimBP를 만들고 `AM_Knight` Montage 재생을 확인한다.
