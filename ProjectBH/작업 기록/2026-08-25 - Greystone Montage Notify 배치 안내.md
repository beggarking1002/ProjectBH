# 2026-08-25 - Greystone Montage Notify 배치 안내

## 목적

Greystone 3타 콤보에서 피해 판정 구간과 콤보 분기 시점을 Unreal Editor Montage에 배치하는 방법을 안내한다.

## Codex 변경 사항

- `ANS Melee Hit Window`를 Attack_A/B/C의 실제 검 접촉 구간에 두는 절차를 개발 가이드로 작성했다.
- `AN Combo Branch`를 Recovery_A/B/C의 끝에 두는 절차와 Montage 섹션 연결 규칙을 작성했다.
- 새 C++ Notify가 메뉴에 보이려면 Live Coding을 종료하고 전체 빌드·에디터 재시작이 필요할 수 있음을 기록했다.

## 사용자 에디터 작업

- `AM_Knight`의 Attack_A/B/C에 구간형 `ANS Melee Hit Window`를 배치한다.
- Recovery_A/B/C 끝에 단발 `AN Combo Branch`를 배치한다.
- Recovery가 다음 Attack으로 자동 연결되지 않도록 Montage 섹션 연결을 확인한다.

## 검증

- 이번 작업은 문서만 변경했다. 새 C++ Notify 클래스의 전체 컴파일은 Live Coding 활성화 상태 때문에 아직 확인하지 못했다.

## 관련 파일

- [[Greystone Montage Notify 배치 절차]]
- [[Greystone 3타 콤보 및 다중 스윕 기반 구현]]

## 남은 작업 / 다음 단계

- Live Coding을 종료한 뒤 전체 C++ 빌드를 통과시킨다.
- Montage Notify와 Socket을 연결하고 2인 PIE에서 콤보·피해 판정을 확인한다.
