# Enemy Corpse Pool

## Understood as

Enemy를 매번 생성·삭제하지 않고 큰 고정 Pool에서 재사용한다. 살아서 AI·전투·Crowd에 참가하는 수는 `ActiveEnemyLimit`로 제한한다. 사망 Enemy는 Slot·AI·Collision을 반납하지만 시각적 시체로 계속 남는다. 교체 스폰 시 Free Enemy가 있으면 그것을 사용해 시체를 보존하고, Free가 없을 때만 가장 오래된 시체 한 구를 Pool Storage로 회수해 같은 개체를 스폰 위치에서 재활성화한다. 기존 5초 `SetLifeSpan` 삭제는 Pool 관리 Enemy에 적용하지 않는다. Unreal Editor의 Manager 배치, Enemy Blueprint Class와 Spawn Point 지정은 사용자가 수행한다.

## Cycle weight

Full. Enemy의 사망·복제·GAS 초기화·Controller 생명주기와 레벨 스폰 정책을 함께 바꾸는 신규 런타임 기능이다.

## Model sizing

```text
recommended_tier: frontier
recommended_effort: thorough
rationale: 네트워크 Actor, AI Controller, GAS Attribute, 사망 연출과 재사용 상태를 한 생명주기로 결합하므로 잘못된 초기화가 유령 AI·중복 Queue·죽은 상태 재사용을 만들 수 있다.
move_up_if: 다중 클라이언트에서 Pool 활성 상태나 재활성화 Transform이 불일치하거나 Gameplay Effect 초기화가 복잡해질 때
move_down_if: Enemy가 비복제 단일 플레이 전용이고 Attribute·Controller 초기화가 제거될 때
proof_surface: UE 5.7 Editor Development 빌드, 상태 전이 코드 대조, Manager 디버그 수치, 사용자 PIE에서 장기 사망·리스폰 반복
```

## Thesis

시체 누적 연출과 안정적인 성능을 동시에 얻으려면 “살아 있는 수”와 “월드에 남은 개체 수”를 분리해야 한다. 시체는 제거 타이머가 아니라 Free Reserve 고갈 시점에만 회수하고, 재사용은 Enemy가 이전 생애의 AI·Slot·Montage·Effect·Health 상태를 전혀 갖지 않는 명시적 Reset 계약을 통과해야 한다.

## Scope

1. `ABHEnemyPoolManager`가 고정 수의 Enemy를 서버에서 미리 생성한다.
2. 상태를 Alive Active, visible Corpse, hidden Free로 나눈다.
3. Alive 수는 Active Limit 이하로 유지하고 사망마다 지연된 교체 요청을 만든다.
4. 교체 시 Free를 우선 사용하며 Free가 없을 때만 가장 오래된 최소 표시시간 경과 시체 한 구를 회수한다.
5. 한 Manager Tick에서 최대 한 명만 재활성화해 시체가 한꺼번에 사라지지 않게 한다.
6. Enemy 재활성화 시 Timer, Montage, Controller, Movement, Collision, Gameplay Effects, Health와 Combat State를 초기화한다.
7. Pool 활성 여부를 복제해 Client의 Hidden·Tick·Collision 표현을 맞춘다.
8. Spawn Point를 순환 사용하고 없으면 Manager 주변 fallback grid를 사용한다.
9. Pool 관리 밖에 놓인 Enemy의 기존 5초 LifeSpan 동작은 보존한다.
10. 현행 SSOT와 날짜별 작업 기록에 Pool 구현 상태와 에디터 절차를 반영한다.

## Out of scope

- 여러 Enemy Class를 한 Manager에서 섞는 가중치 스폰
- Wave·난이도·보스 스케줄러
- 카메라 가시성 기반 시체 선택
- Save/Load
- Blueprint `.uasset` 생성·수정
- Replication Graph와 Dormancy 최적화

## Quality gates

1. Pool Capacity가 실제 Spawn 성공 수를 넘지 않으며 Alive 수가 Active Limit을 넘지 않는다.
2. Free가 하나라도 있으면 시체를 회수하지 않는다.
3. Free가 없을 때 한 번의 처리에서 가장 오래된 eligible Corpse 하나만 회수한다.
4. Pool 관리 Enemy는 사망 5초 뒤 삭제되지 않고 시체로 남는다.
5. 재활성화 Enemy는 최대 Health, Chasing, 정상 Collision·Movement·Controller 상태로 돌아간다.
6. 이전 생애의 Slot·Queue·Attack Target·Timer·Montage·Gameplay Effect가 남지 않는다.
7. Client가 inactive Enemy를 숨기고 재활성화 Enemy와 Death 상태를 정상 표현한다.
8. Enemy Class나 Spawn Point 설정 오류가 Crash 대신 명확한 로그와 안전한 fallback을 만든다.
9. UHT, C++ 컴파일과 Editor Development 링크가 성공한다.
10. README/CLAUDE 존재 여부와 문서 역할을 확인하고 내부 규칙은 Obsidian SSOT에 반영한다.

