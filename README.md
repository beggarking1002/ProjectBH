# ProjectBH

Unreal Engine 5 C++로 제작한 온라인 협동 액션 전투 프로토타입입니다.  
다수의 근접 Enemy가 플레이어 주변의 공간과 전투 상황에 맞춰 이동 대형과 공격 순서를 조정하도록 구현했습니다.

## 프로젝트 정보

- 개발 기간: 2026.08.20 - 2026.09.04
- 개발 인원: 1인
- 담당 범위: 프로그래밍 및 시스템 기획 전체
- 개발 환경: Unreal Engine 5.7, C++
- 에셋: 무료 에셋 사용

## 시연 영상

[YouTube에서 ProjectBH 시연 영상 보기](https://www.youtube.com/watch?v=QXBKnts_jN0)

## 핵심 구현

- Attack, Wait, Holding Slot을 이용한 공격권 배분과 교대
- Open, Corridor, Pocket 공간 판정과 전투 대형 재구성
- 경로 실패 시 우회, Core Escape, Slot 재배정을 통한 개별 Enemy 복구
- 대형 Enemy의 부채꼴 공간 확보와 최소 양보 대상 계산
- 서버 권한 기반 근접 공격, 콤보, 피격 및 체력 처리
- Data Asset과 Gameplay Tag를 활용한 데이터 주도 설정

## 주요 코드

- `Source/ProjectBH/Components/Combat/CombatEngagementSlotComponent.*`  
  전투 Slot 관리, 공간 판정, 예약 교대와 대형 갱신
- `Source/ProjectBH/Combat/Engagement/BHLargeEnemyEngagementPolicy.*`  
  대형 Enemy의 부채꼴 공간 및 Attack 예약 정책
- `Source/ProjectBH/AI/BHCrowdEnemyAIController.*`  
  예약 위치 이동, 우회와 복구 단계 제어

## Contact

- 주재형
- <drake741236@gmail.com>
