# Enemy 전용 Capsule 충돌 분리 재도입

## 결정

Enemy끼리 Capsule 물리 Block을 사용하지 않는다. 같은 Enemy 전용 채널끼리는 Ignore하고, 지형과 Player에 대한 Block은 유지한다.

## 근거

- 접촉한 두 Enemy가 서로 밀면서 목적지 이동과 Attack Slot 복구를 방해하는 사례가 반복되었다.
- 첨부 화면의 `Spacing:0`은 Capsule이 관통하지 않았다는 뜻이지, Capsule이 맞닿아 서로의 진행을 막지 않았다는 뜻은 아니다.
- watchdog과 Slot 교대는 교착 이후의 복구다. 하드 Block을 제거하면 접촉 자체가 이동을 정지시키는 원인을 줄일 수 있다.
- 대신 실제 간격 보장은 Detour Crowd Obstacle Avoidance, Separation과 Slot 배치가 담당해야 한다.

## 이전 롤백과 차이

이전 실험은 전용 채널의 기본 응답 때문에 지형 충돌까지 흔들렸다. 이번 구현은 `EnemyPawn` 채널의 기본 응답을 Block으로 두고 살아 있는 Enemy Capsule에서 다음 응답만 명시한다.

| 상대 | 응답 |
| --- | --- |
| EnemyPawn | Ignore |
| WorldStatic | Block |
| WorldDynamic | Block |
| Pawn, Player | Block |

## 구현

- `ECC_GameTraceChannel1`을 `EnemyPawn` Object Channel로 등록했다.
- Enemy 생성·BeginPlay·Pool 재활성화 때 Capsule Object Type과 응답을 다시 적용한다.
- Hero Capsule은 `EnemyPawn`을 Block한다.
- Hero Sword Trace Object Query에 `EnemyPawn`을 추가했다.
- 기존 Detour Crowd와 RVO Off 정책은 바꾸지 않았다.

### 영향 범위 점검

- C++ 전체에서 `ECC_GameTraceChannel1`의 기존 사용은 없었다.
- C++ 전체에서 `ECC_Pawn`만 조회하던 Object Query는 Hero Sword Trace 한 곳이며 `EnemyPawn`을 함께 조회하도록 수정했다.
- 모든 현행 Enemy는 `ABHEnemy` 파생형이므로 공통 BeginPlay와 Pool 재활성화 경로에서 설정을 받는다.
- Skeletal Mesh·무기·보조 Collision Component의 응답은 바꾸지 않았다. 이동을 막는 Root Capsule만 이번 정책의 범위다.
- Blueprint 그래프 안의 `Pawn` Object Query는 바이너리 Asset이라 정적 검색만으로 전수 보장하지 않는다. 이후 Blueprint Trace·Overlap을 추가할 때 `EnemyPawn` 포함 여부를 확인해야 한다.

## PIE 확인

Collision Channel 설정은 에디터 시작 때 로드되므로 Unreal Editor를 완전히 재시작한 뒤 확인한다.

1. `show collision`, `bh.Debug.Enabled 1`, `bh.Debug.Crowd 1`, `bh.Debug.Slots 1`을 켠다.
2. 12마리로 Player 입력을 멈춘 채 고밀도 교차 상황을 `30초 × 3회` 관찰한다. 같은 두 Enemy가 서로 맞닿은 채 양쪽 모두 목표 진행 없이 `4.5초` 넘게 유지되는지 확인한다.
3. Enemy가 바닥·벽을 통과하지 않는지 확인한다.
4. Player가 Enemy Capsule을 통과하지 않는지 확인한다.
5. Player 검 공격으로 Enemy Health가 실제 감소하고 한 번에 최대 3명 타격 규칙이 유지되는지 확인한다.
6. 대형이 정착한 뒤 `Spacing` 현재값이 0으로 돌아오는지 확인한다. 1초를 넘겨 유지되는 위반 쌍은 Crowd·Slot 간격 결함으로 기록한다.
7. 신규 배치 Enemy와 Pool 재활성화 Enemy를 각각 확인한다.
8. 사망 후 Capsule Off와 Pool 재활성화 후 Collision 복구를 확인한다.
9. Blueprint로 구현된 Trace·Overlap·감지 기능이 있다면 Object Types에 `EnemyPawn`이 포함됐는지 확인한다.

## 합격 기준

- 고밀도 `30초 × 3회`에서 같은 두 Enemy가 접촉한 채 양쪽 모두 `4.5초` 이상 목표 진행 없이 남지 않는다.
- Enemy는 지형과 Player를 통과하지 않는다.
- Player 공격 Trace가 Enemy Health를 정상 감소시킨다.
- 대형 정착 뒤 `Spacing` 현재값은 0이며, 위반 쌍이 1초 이상 유지되지 않는다.
- Enemy끼리 Ignore한 결과 시각적 중첩이 과도해지면 Collision을 다시 켜기보다 Separation과 Slot 후보 간격을 먼저 조정한다.

Enemy끼리 Ignore하는 것은 Capsule 접촉에 의한 상호 밀기만 제거한다. Slot 중복, 잘못된 경로, 공간 부족과 공격 상태 잠금으로 생기는 정지는 별도 원인이므로 이번 변경 하나가 모든 교착을 없앤다고 보장하지 않는다.
