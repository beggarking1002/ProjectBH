# 2026-08-27 - MedCastle 에셋 Git 제외

## 목적

외부에서 다시 받을 수 있는 Medieval Castle 환경 에셋을 ProjectBH Git 저장소에서 제외한다.

## Codex 변경 사항

- 언리얼 콘텐츠 경로 `/Game/MedCastle`에 대응하는 실제 폴더 `/Content/MedCastle/`을 `.gitignore`에 추가했다.

## 사용자 에디터 작업

- 없음.

## 검증

- `Content/MedCastle` 폴더가 현재 프로젝트에 존재하는 것을 확인했다.
- 해당 폴더 아래에 이미 Git이 추적 중인 파일은 없음을 확인했다.

## 관련 파일

- `.gitignore`

## 남은 작업 / 다음 단계

- 다른 환경 에셋 폴더도 제외할 경우 언리얼 `/Game/...` 경로를 실제 `/Content/.../` 경로로 변환해 개별 규칙으로 추가한다.
