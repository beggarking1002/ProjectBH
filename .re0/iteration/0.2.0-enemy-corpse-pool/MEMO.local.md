# Enemy Corpse Pool Cycle Memo

## 판정

Keep and drive. C++ 수명 주기와 빌드 계약은 유지할 가치가 있다. 다만 장기 PIE와 네트워크 관찰이 아직 없으므로 런타임 완료로 판정하지 않는다.

## 보존할 계약

1. 전체 Actor 예산과 살아서 행동하는 AI 예산을 분리한다.
2. 상태는 Alive, Corpse, Free 세 가지이며 항상 `실제 생성 수 = Alive + Corpse + Free`를 만족해야 한다.
3. 보충은 Free를 먼저 쓰고, Free가 없을 때만 가장 오래된 최소 표시시간 경과 시체 한 구를 재사용한다.
4. Manager는 수량·순서를 소유하고 Enemy는 한 생애의 Reset을 소유한다.
5. 빌드 증거와 PIE 증거를 분리한다.

## 배운 점

### 시체 예산은 별도 숫자가 아니라 Free 소비 결과다

기본값 40/12에서 시체가 누적될수록 Free가 28에서 0으로 줄고, 이후 시체는 28구를 중심으로 유지된다. Free를 별도 고정 Reserve로 해석하면 사용자의 “여분이 없을 때만 시체 회수” 규칙과 충돌한다.

### Pooling은 Spawn 최적화보다 생애 격리가 핵심이다

Actor만 숨겼다가 보이면 이전 Controller, Slot, Timer, Montage, Effect와 Health가 다음 생애로 샐 수 있다. 재활성화 API가 이 전체 계약을 한곳에서 수행해야 한다.

### 실제 생성 수가 런타임 Capacity다

설정값만 디버그에 표시하면 일부 Spawn 실패가 수량 불일치를 감춘다. Manager는 `PoolEnemies.Num()`을 effective capacity로 사용하고 `Alive + Corpse + Free` 합계를 함께 표시해야 한다.

## 금지할 안티패턴과 Gate

| 안티패턴 | 실패 형태 | 다음 Gate |
| --- | --- | --- |
| Timer Corpse Cleanup | 여분이 남아도 시체가 자동 소멸 | Pool 관리 `Die()`에서 양수 LifeSpan 금지 |
| Inactive 상태 통합 | 보이는 Corpse와 숨은 Free를 같은 수로 계산 | 세 상태 합계 불변식 표시 |
| 부분 Reset | 죽은 Health·Controller·Montage가 재사용 생애에 잔존 | 동일 Actor의 사망→재등장→공격 PIE |
| Prewarm Controller 생성 | 숨은 28명도 Controller와 Crowd 비용 사용 | deferred spawn 전에 `AutoPossessAI` 비활성화 |
| 월드 밖 Storage | Kill Z/World Bounds가 숨은 Pool Actor를 제거 | Hidden·Collision Off 상태로 Manager 근처 보관 |
| 자체 Debug만 검증 | 계산 코드가 자기 계산을 통과 | 월드 개체·AI 행동·Client 화면과 독립 대조 |

## 다음 검증 첫 순서

1. 40/12 초기 수량과 숨은 Free 28명을 대조한다.
2. 첫 사망 후 시체가 5초를 넘어 유지되고 Free 하나로만 보충되는지 본다.
3. Free 0에서 가장 오래된 시체 한 구만 사라지는지 본다.
4. 재사용된 동일 Actor가 MaxHealth, 새 Controller, Chasing 상태로 다시 공격하는지 본다.
5. Listen Server Client와 Late Join에서 시체 Pose와 재등장 표현을 확인한다.
