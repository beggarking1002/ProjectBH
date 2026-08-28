# Ring Ingress Waypoints

## Understood as

Gate를 복구하지 않고, 안쪽 링으로 이동하는 Enemy가 목표 슬롯의 각도에 먼저 정렬한 뒤 방사형으로 진입하도록 이동 목표 생성을 변경한다. Enemy 전용 충돌 채널은 이 사이클의 범위에서 제외하고, 분산형 진입로만으로 교착이 얼마나 감소하는지 먼저 검증한다.

## Thesis

정지한 슬롯 점유자 사이를 직선으로 관통하는 목표 생성을 없애면, Detour Crowd에 더 강한 분리력을 요구하지 않고도 진입 교착을 줄일 수 있다. 각 Enemy가 서로 다른 슬롯 각도를 사용하는 분산형 진입점은 단일 Gate의 수렴 병목을 만들지 않는다.

## Scope

- `CombatEngagementSlotComponent`의 예약 슬롯 이동 목표 생성을 링 전환 단계를 인식하도록 변경한다.
- Holding은 현재 링으로 진입한 뒤 각도를 맞춘다.
- Wait는 Holding Ring 또는 안전한 바깥 링에서 목표 Wait Slot 각도를 맞춘 뒤 안쪽으로 진입한다.
- Attack은 Wait Ring에서 목표 Attack Slot 각도를 맞춘 뒤 안쪽으로 진입한다.
- 기존 Combat Core 보호, 슬롯 예약, 승격 순서, 교착 watchdog은 유지한다.
- Enemy 충돌 채널 변경은 제외한다.

## Quality gates

1. **Distributed route gate:** 모든 에이전트가 하나의 공유 지점으로 수렴하지 않고 자신의 최종 슬롯 각도를 기반으로 진입점을 계산한다.
2. **Monotonic staging gate:** 바깥 링 정렬과 안쪽 링 진입이 명확히 구분되며, 진입 단계가 영구적으로 왕복하지 않는다.
3. **No regression gate:** 직접 진입이 안전한 적은 불필요한 우회를 하지 않고, Combat Core를 관통하지 않는다.
4. **NavMesh gate:** 모든 중간 목표는 NavMesh에 투영되며 투영 실패 시 기존 안전한 경로로 대체한다.
5. **Build gate:** `ProjectBHEditor Win64 Development` UHT, compile, link가 성공한다.
6. **Real-surface gate:** UE Editor에서 4/8/12/13+마리를 실행하여 Wait→Attack, Holding→Wait 진입로와 Stuck 발동을 확인할 수 있는 Debug 표시가 제공된다.
7. **Documentation reflection gate:** 내부 메커니즘은 Obsidian 군중 설계 문서에, 검증 절차와 결과는 작업 기록에 반영한다. 루트 `CLAUDE.md`/`README`는 현재 없으며 새 사용자 기능을 추가하는 작업이 아니므로 새로 생성하지 않는다.

## Model sizing

```text
recommended_tier: standard
recommended_effort: thorough
rationale: 수정은 소수 모듈에 한정되고 되돌리기 쉬우나, 기하 경계조건과 이전 Gate 병목의 재발을 방지하기 위한 충분한 검토가 필요하다.
move_up_if: 충돌 채널·Crowd Manager 수정으로 범위가 넓어지거나 런타임 영상만으로 원인을 역추적해야 한다면 상향한다.
move_down_if: 기존 함수의 단순 수치 변경만으로 재현과 해결이 완전히 증명되면 하향한다.
proof_surface: 엔진 빌드, 경로 단계 Debug 표시, UE Editor의 4/8/12 마리 승격 재현 테스트가 필요하다.
```
