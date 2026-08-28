# 2026-08-28 - 시체 누적형 Enemy Object Pool 구현

## 목적

Enemy를 사망 5초 뒤 삭제하지 않고 전장에 시체로 누적한다. 전체 Enemy Actor는 큰 고정 Pool로 미리 만들고, 살아서 AI·전투·Crowd에 참가하는 수만 제한한다. 교체 스폰에 사용할 Free Enemy가 없을 때에만 시체 한 구를 회수해 재사용한다.

## 구현 구조

### `ABHEnemyPoolManager`

- 서버에서 `EnemyClass`를 `PoolCapacity`만큼 deferred spawn으로 미리 만든다.
- 상태를 Alive Active, visible Corpse, hidden Free Reserve로 분리한다.
- 사망으로 Alive가 부족해지면 `RespawnDelay` 뒤 준비되는 교체 요청을 만든다.
- 한 Tick에 최대 한 명만 교체한다. 준비 시간이 지난 요청은 Manager의 다음 `0.25초` Tick에서 처리되므로 기본 5초 보충에는 최대 약 0.25초의 지연이 붙을 수 있다.
- 서버 화면의 Manager 머리 위에 `실제 생성 수/설정 Capacity`, `Alive / Free / Corpses / Pending`과 상태 합계를 출력한다.

### `ABHEnemy`

- Pool 관리 Enemy는 사망할 때 `DeadActorLifeSpan`을 사용하지 않는다.
- 사망 즉시 Slot·Queue, Movement와 AI Controller를 반납하고 Death Montage를 재생한다.
- `PoolInWorld`를 복제해 Free Storage의 Hidden·Collision·Tick 상태를 Client에도 적용한다.
- Free Actor는 Kill Z/World Bounds 밖으로 내리지 않고 Manager 근처에 숨겨 보관한다.
- 재활성화 때 Timer, Montage, Gameplay Effect, Health, Combat State, Movement, Collision과 Controller를 초기화한다.

## 기본 수치

| 항목 | 값 |
| --- | ---: |
| Pool Capacity | 40 |
| Active Enemy Limit | 12 |
| Respawn Delay | 5초 |
| Minimum Corpse Display Time | 5초 |
| Manager Tick | 0.25초 |
| Fallback Spawn Spacing | 175 cm |

Pool의 수량 불변식은 다음과 같다.

```text
Pool Capacity = Alive + Corpse + Free
Alive <= Active Enemy Limit
```

기본값에서 장기 전투가 계속되면 `Alive 12 + Corpse 28 + Free 0`을 중심으로 유지된다.

## 시체 회수 규칙

1. 교체 스폰 요청이 아직 5초 지연 중이면 아무것도 회수하지 않는다.
2. 요청이 준비됐고 Free가 있으면 Free Enemy를 사용한다. 모든 시체를 보존한다.
3. Free가 0이면 최소 5초 표시된 시체 중 가장 오래된 한 구를 고른다.
4. eligible Corpse가 없으면 최소 표시시간을 깨지 않고 스폰을 미룬다.
5. 선택한 시체만 숨겨 Storage 상태로 만든 뒤 같은 Actor를 Spawn Point에서 재활성화한다.
6. 다음 시체 회수는 다음 Manager Tick 이후에만 가능하다.

## 사용자 에디터 작업

1. Unreal Editor를 재시작한다.
2. `FeatureDevMap`에서 기존에 직접 배치한 테스트 Enemy를 제거하거나 별도 구역으로 옮긴다. 직접 배치 Enemy는 Pool Limit에 포함되지 않는다.
3. Place Actors의 All Classes에서 `BHEnemyPoolManager`를 찾아 Level에 배치한다. 수치를 Level 인스턴스에서 관리할 때는 C++ Actor를 직접 쓰고, 여러 Level에서 같은 Preset을 재사용할 때만 이를 부모로 `BP_EnemyPoolManager`를 만든다. 같은 전투 구역에는 Manager 하나만 둔다.
4. 배치한 Manager의 Details에서 `Enemy Pool|Setup > Enemy Class`를 `/Game/Enemy/BP_BHEnemy`로 지정한다.
5. `Pool Capacity = 40`, `Active Enemy Limit = 12`, `Respawn Delay = 5`, `Minimum Corpse Display Time = 5`로 시작한다.
6. 등장 위치를 직접 통제하려면 `Place Actors`에서 `Target Point`를 검색해 Level로 드래그한다. Level의 `BHEnemyPoolManager` 인스턴스를 선택하고 Details 패널의 `Enemy Pool|Spawn > Spawn Points` 배열에 `+`로 원소를 추가한 뒤 스포이트로 각 Point를 등록한다. 초기 Enemy도 배열을 순환하므로 12명을 동시에 시작할 때는 서로 떨어진 Point 12개를 두는 것이 안전하다. Point를 적게 두면 같은 위치에서 여러 명이 겹칠 수 있다.
7. 등장 위치를 따로 정할 필요가 없으면 `Spawn Points`를 비워 둔다. 이 경우 Manager 위치·회전을 기준으로 175 cm 간격의 fallback grid를 만들고 NavMesh에 투영한다. 첫 기능 검증에는 이 방식이 가장 간단하다.
8. Target Point와 fallback grid 모두 위치를 기존 NavMesh에 투영할 뿐 NavMesh 자체를 만들지는 않는다. Spawn 위치에서 플레이어 주변 Attack/Wait/Holding Ring까지 녹색 NavMesh가 연결되는지 `P` 키로 확인한다.
9. Death Montage가 끝난 뒤 마지막 프레임을 유지하도록 기존 Montage·AnimBP 사망 설정을 확인한다.

## PIE 검증 순서

### 1. 초기 상태

- Manager 머리 위가 `Enemy Pool 40/40 | Alive:12/12 Free:28 Corpses:0 Pending:0 | Sum:40 OK`인지 확인한다.
- 살아 있는 Enemy가 12명을 넘지 않는지 확인한다.

### 2. Free Reserve 사용

1. Enemy 한 명을 죽인다.
2. 즉시 `Alive 11 / Free 28 / Corpses 1 / Pending 1`인지 확인한다.
3. 시체가 5초 뒤에도 사라지지 않는지 확인한다.
4. 약 5초 뒤 교체 Enemy가 Spawn Point에 나타나고 `Alive 12 / Free 27 / Corpses 1 / Pending 0`이 되는지 확인한다.

### 3. Free 고갈과 시체 단일 회수

1. 사망과 교체를 반복해 `Alive 12 / Free 0 / Corpses 28`을 만든다.
2. 한 명을 더 죽여 `Alive 11 / Free 0 / Corpses 29 / Pending 1`을 확인한다.
3. 교체 시간이 되면 가장 오래된 시체 한 구만 사라지고 새 Enemy 한 명만 Spawn되는지 확인한다. `Output Log`의 `spawned replacement ... from oldest corpse`와 World Outliner의 동일 Actor 이름을 함께 대조한다.
4. 결과가 다시 `Alive 12 / Free 0 / Corpses 28 / Pending 0`인지 확인한다.

### 4. 재사용 상태

- 재활성화 Enemy를 PIE 중 선택해 Details/Blueprint Debug 또는 GAS 디버그에서 Health가 MaxHealth인지 확인한다.
- `CombatState = Chasing`이고 새 AI Controller가 추적과 슬롯 요청을 다시 시작하는지 이동·공격으로 확인한다.
- 이전 Death/Attack Montage, Target, Slot, Queue Sequence와 Gameplay Effect가 남지 않는지 확인한다.
- Listen Server에서 Client에도 시체 유지·단일 회수·재등장이 동일하게 보이는지 확인한다.

## 현재 한계

- 한 Manager는 하나의 Enemy Class만 지원한다.
- 카메라 밖·플레이어 거리 기반 시체 선택은 아직 없고 가장 오래된 시체를 사용한다.
- Material, Dissolve와 부위 파괴 같은 Blueprint 전용 상태는 자동 Reset하지 않는다.
- 늦게 접속한 Client의 시체 마지막 Pose 복원은 별도 검증·보강 대상이다. 현재 복제는 Pool 가시성·Collision 상태를 맞추지만 이미 끝난 Death Montage 자체를 재전송하지 않는다.
- Dormancy와 Replication Graph 최적화는 아직 적용하지 않았다.
- 실제 장기 PIE와 네트워크 검증은 사용자가 수행해야 한다.

## 빌드 검증

- UE 5.7 UnrealHeaderTool 성공
- `ProjectBHEditor Win64 Development` C++ 컴파일 성공
- `UnrealEditor-ProjectBH.dll` 링크 성공
- 결과: `Succeeded`

## 관련 파일

- `Source/ProjectBH/Enemies/BHEnemyPoolManager.h/.cpp`
- `Source/ProjectBH/Enemies/BHEnemy.h/.cpp`
- [[몬스터 이동 시스템 규칙]]
