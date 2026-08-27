# 2026-08-27 - NavMesh Bounds Volume 배치 가이드 작성

## 목적

단일 Enemy NavMesh 추적의 에디터 검증을 위해 `NavMeshBoundsVolume` 배치와 문제 확인 절차를 문서화한다.

## Codex 변경 사항

- `개발 가이드/NavMesh Bounds Volume 배치 및 검증.md`를 추가했다.
- 볼륨 배치, `P` 키 시각화, Enemy 설정, 정상 동작 기준과 NavMesh가 생성되지 않을 때의 점검 순서를 정리했다.

## 사용자 에디터 작업

- 가이드에 따라 테스트 맵을 덮는 `NavMeshBoundsVolume`을 하나 배치한다.
- 플레이어와 Enemy 사이의 바닥이 연속된 녹색으로 표시되는지 확인한다.
- `BP_Enemy_Base` 한 마리로 추적을 실행한다.

## 검증

- UE 공식 Navigation 문서와 배치·시각화 절차를 대조했다.
- 실제 ProjectBH 맵의 런타임 검증은 사용자 확인 대기다.

## 관련 파일

- `ProjectBH/개발 가이드/NavMesh Bounds Volume 배치 및 검증.md`
- `ProjectBH/작업 기록/2026-08-27 - 단일 적 NavMesh 추적 기반 구현.md`

## 남은 작업 / 다음 단계

- Enemy의 추적, 정지 거리, AnimBP 값을 PIE에서 검증한다.
