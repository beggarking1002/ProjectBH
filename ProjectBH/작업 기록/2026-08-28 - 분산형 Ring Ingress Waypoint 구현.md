# 분산형 Ring Ingress Waypoint 구현

## 목적

안쪽 Slot으로 승격된 Enemy가 이미 정지한 바깥 Ring 점유자 사이를 직선으로 관통하려다 끼는 문제를 완화한다.

## 구현

- `EBHCombatMoveRouteStage`를 `Direct`, `ApproachRing`, `AlignOnRing`, `Ingress`로 구분했다.
- Attack Slot 이동은 Wait Ring을, Wait Slot 이동은 Holding Ring을 진입 정렬 Ring으로 사용한다.
- Enemy는 자신의 목표 Slot 각도에 해당하는 Ring 지점으로 이동한 후 방사형으로 진입한다.
- 모든 Enemy가 하나의 Gate를 공유하지 않으므로 진입 목표가 Slot 각도별로 분산된다.
- `Ingress`가 시작된 뒤에는 같은 Slot 예약 중 바깥 Ring으로 되돌아가지 않는다.
- Slot 종류·인덱스 변경, Formation Reform, Slot 반납, 교착 교대 시 Route Stage를 `Direct`로 초기화한다.
- Ring Waypoint는 5 cm, 최종 Slot은 15 cm의 수용 반경을 사용한다.
- Ring 진입 각도 수용 오차 기본값은 1도다.
- 중심에 정확히 겹친 Enemy는 반지름 0을 유지한 채 목표의 반대 방향으로 진입로를 잡는다.
- 안쪽 Ring 도착 기준은 이론상 반경이 아니라 NavMesh에 투영된 실제 최종 Slot 반경을 사용한다.

## Debug 표시

| 단계 | 경로선 색 |
| --- | --- |
| Direct | 흰색 |
| ApproachRing | 청록 |
| AlignOnRing | 파랑 |
| Ingress | 주황 |

Enemy 머리 위 `Route` 텍스트에도 현재 단계가 표시된다.

## 검증

- UE 5.7 `ProjectBHEditor Win64 Development` 빌드에서 UHT, C++ 컴파일, DLL 링크가 성공했다.
- `git diff --check`에서 공백 오류는 발견되지 않았다. 기존 파일의 LF/CRLF 전환 경고만 출력되었다.

## PIE 확인 절차

1. 4마리로 Attack Slot 진입과 공격을 확인한다.
2. 8마리로 Wait Ring을 채운 뒤 Wait→Attack 승격을 발생시킨다.
3. 12마리로 Attack 4 + Wait 8이 모두 채워지는지 확인한다.
4. 13마리 이상으로 Holding Ring을 사용하고 Holding→Wait 승격을 발생시킨다.
5. 승격 Enemy의 경로선이 청록→파랑→주황으로 바뀌는지 확인한다.
6. 여러 Enemy의 파란 경로 목표가 하나의 지점이 아니라 각자의 Slot 각도로 분산되는지 확인한다.
7. Enemy가 여전히 끼면 해당 Enemy의 `Route`, `Stuck`, Slot 종류·인덱스를 기록한다.

## 보류 사항

- Enemy 캡슐끼리의 하드 충돌을 분리하는 `EnemyPawn` Object Channel은 이번 변경에 포함하지 않았다.
- 분산형 진입로만으로 교착이 충분히 줄지 않을 때만 전용 충돌 채널을 다음 대책으로 검토한다.
