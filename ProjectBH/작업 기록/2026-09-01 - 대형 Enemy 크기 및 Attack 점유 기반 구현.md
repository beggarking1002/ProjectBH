# 대형 Enemy 크기 및 Attack 점유 기반 구현

## 목표

- 별도 대형 C++ 자식 클래스를 만들지 않고 기존 `ABHEnemy`와 Enemy Data Asset으로 크기를 구분한다.
- 일반 Enemy의 현행 Slot·AI 동작을 유지하면서 대형 Enemy가 더 많은 Attack 수용량과 공간을 소비할 기반을 만든다.
- 대형 Blueprint가 기존 Detour Crowd, 공격, 사망 및 Object Pool 생명주기를 그대로 사용할 수 있게 한다.

## 구현 내용

### Enemy Data Asset

`UDataAsset_EnemyConfig`에 다음 설정을 추가했다.

- `Size Class`: `Normal`, `Large`
- `Capsule Radius Override`
- `Capsule Half Height Override`
- `Attack Slot Cost`
- `Attack Slot Exclusion Radius`
- `Max Concurrent Attackers Of Size`

기존 Data Asset은 `Normal / 비용 1 / 추가 간격 0 / 동시 상한 없음 / Capsule Override 0`을 사용하므로 기존 일반 Enemy 동작이 유지된다.

### Enemy 런타임 설정

- `ABHEnemy`가 크기 등급과 Attack 점유 설정을 Blueprint에서도 읽을 수 있는 Getter를 제공한다.
- BeginPlay와 Object Pool 재활성화 때 Data Asset의 이동속도와 선택적 Capsule Override를 적용한다.
- Capsule Override가 적용되면 Character Movement의 Nav Agent 속성을 Capsule 크기로 갱신한다.

### Attack Slot 입장 규칙

- 활성 Attack Slot 수를 전열의 총 비용 예산으로 사용한다.
- 기존 Attack 비용과 신규 후보 비용의 합이 예산을 넘으면 입장을 거부한다.
- 같은 크기 등급 동시 Attack 상한과 점유 Slot 중심 간 최소 거리를 검사한다.
- 검사는 직접 예약, 초기 대형, Wait 승격, 빠른 Attack 인계, Combat Core 침입 교대, Corridor 및 Pocket 재편 경로에 연결했다.
- 크기 비용 또는 추가 간격을 사용하는 Attack 소유자는 일반 거리 절감 Reform에서 자리를 교환하지 않게 해, 교환 후 점유 규칙이 깨지는 것을 막았다.
- 비용 예산이 이미 가득 찼거나 기존 대형의 제외 반경으로 사용할 수 없는 빈 Attack Slot은 vacancy fallback 시간을 누적하지 않는다.
- 해당 Slot은 디버그에서 진한 보라색으로 그리고 상단 문자열에 Attack 비용과 차단 Slot 수를 표시한다. 물리적으로 빈 Slot이 초록색으로 남아 예약 오류처럼 보이는 상황을 구분한다.

## Object Pool 호환성

기존 `ABHEnemyPoolManager`는 `TSubclassOf<ABHEnemy>`를 받으므로 `BP_Enemy_Large`를 그대로 사용할 수 있다. 현재 Manager 하나는 한 Enemy Class만 생성하므로 일반과 대형은 별도 Pool Manager로 운용한다. 다종 혼합 가중치 Pool은 이번 범위에 포함하지 않았다.

## 대형 Data Asset 권장 시작값

| 설정 | 시작값 |
| --- | ---: |
| Size Class | Large |
| Capsule Radius Override | 실제 Mesh 확인 후 60~75 cm |
| Capsule Half Height Override | 실제 Mesh 확인 후 입력 |
| Attack Slot Cost | 2 |
| Attack Slot Exclusion Radius | 우선 0, 실제 겹침 확인 후 조정 |
| Max Concurrent Attackers Of Size | 1 |

`Attack Slot Exclusion Radius`를 Open 기본 5각형의 인접 Slot 거리보다 크게 잡으면 양쪽 인접 Slot이 모두 막힐 수 있다. 따라서 Capsule과 공격 자세를 연결한 뒤 디버그 Slot 중심 거리를 보며 올린다.

## 사용자가 에디터에서 할 작업

1. `ABHEnemy` 기반 `BP_Enemy_Large`를 만든다.
2. 대형 Skeleton을 사용하는 `ABP_Enemy_Large`를 만들고 부모 클래스를 `BHEnemyAnimInstance`로 둔다.
3. `DA_Enemy_Large_Config`를 만들고 위 권장값을 입력한다.
4. Blueprint의 `Enemy Config Data Asset`, Mesh, Anim Class, AI Controller를 연결한다.
5. Object Pool 테스트 시 별도 Pool Manager에 `BP_Enemy_Large`를 Enemy Class로 지정한다.

## 검증

- UnrealHeaderTool 처리 성공.
- `ProjectBHEditor Win64 Development` C++ 컴파일과 DLL 링크 성공.
- 실제 대형 Mesh, 공격 Montage, Capsule 수치와 혼합 군중 PIE 검증은 대기 중이다.

## 현재 남은 범위

- 대형 전용 Attack Ring 반경
- 크기 혼합형 Wait·Holding·Pending Row 재배치
- 대형 전용 Supported Nav Agent/NavData 필요성 검증
- 일반·대형을 한 Manager에서 가중치로 생성하는 혼합 Object Pool
