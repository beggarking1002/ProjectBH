# IK Rig 공통 리타기팅 체인 표준

## 목적

ProjectBH에서 새 직립형 캐릭터의 IK Rig와 IK Retargeter를 만들 때 사용하는 공통 기준이다. 체인 이름을 통일해 같은 이름끼리 매핑하고, 팔·다리·머리·손가락 동작이 누락되지 않게 한다.

이 문서는 `IK_Sparrow`, `IK_SM_Countess`, `IK_TwinBlast`, `IK_Khaimera`에 적용한 구성을 기준으로 한다.

## 적용 범위

- `root → pelvis → spine` 계층과 Epic/Paragon 계열의 손·발·손가락 본 이름을 가진 캐릭터
- 서로 다른 Skeleton이라도 아래 본 이름이 대응되는 경우

다음 경우에는 이 표준을 그대로 강제하지 않는다.

- 손가락 본이 없거나 손가락 수가 다른 캐릭터
- 비인간형, 다족형, 매우 큰 체형
- 꼬리·날개·망토처럼 캐릭터 전용 추가 본

전용 본은 삭제하지 않는다. 공용 리타기팅이 필요할 때만 별도 체인을 추가한다.

## Retarget Root

두 IK Rig 모두 **Retarget Root를 `pelvis`**로 설정한다.

`root`는 Root Motion 처리용 골격 루트이고, Retarget Root와 같은 의미가 아니다.

## 필수 체인

| 체인 이름 | Start Bone | End Bone | 이유 |
| --- | --- | --- | --- |
| Spine | `spine_01` | `spine_03` | 상체·가슴 회전 전달 |
| LeftArm | `clavicle_l` | `hand_l` | 쇄골까지 포함해 활·총·양손무기 자세 전달 |
| RightArm | `clavicle_r` | `hand_r` | 쇄골까지 포함해 활·총·양손무기 자세 전달 |
| LeftLeg | `thigh_l` | `foot_l` | 보행·점프 발 동작 전달 |
| RightLeg | `thigh_r` | `foot_r` | 보행·점프 발 동작 전달 |
| Neck | `neck_01` | `neck_02` | 목 회전 전달 |
| Head | `head` | `head` | 머리 단일 뼈 회전 전달 |

팔 체인은 `upperarm`에서 시작하지 않는다. 활 당기기·조준처럼 쇄골 회전이 큰 동작에서 팔이 틀어지는 것을 막기 위해 반드시 `clavicle`부터 시작한다.

## 손가락 체인

손을 쥐는 공격, 총 파지, 아이템 사용 애니메이션을 리타기팅할 경우 좌우 손가락 체인도 기본으로 넣는다.

| 체인 이름 | Start Bone | End Bone |
| --- | --- | --- |
| LeftThumb | `thumb_01_l` | `thumb_03_l` |
| LeftIndex | `index_01_l` | `index_03_l` |
| LeftMiddle | `middle_01_l` | `middle_03_l` |
| LeftRing | `ring_01_l` | `ring_03_l` |
| LeftPinky | `pinky_01_l` | `pinky_03_l` |
| RightThumb | `thumb_01_r` | `thumb_03_r` |
| RightIndex | `index_01_r` | `index_03_r` |
| RightMiddle | `middle_01_r` | `middle_03_r` |
| RightRing | `ring_01_r` | `ring_03_r` |
| RightPinky | `pinky_01_r` | `pinky_03_r` |

손가락 뼈가 없는 Skeleton은 해당 체인만 생략하고, 손가락을 펴 둔 기본 자세가 결과에 남을 수 있음을 기록한다.

## 알려진 Skeleton 예외

| 캐릭터 | 예외 | 적용 방식 |
| --- | --- | --- |
| Murdock | `neck_02`가 없음 | `Neck`: `neck_01 → neck_01` 단일 뼈 체인 |

체인 **이름**은 `Neck`으로 유지해 IK Retargeter의 공통 매핑을 깨지 않는다. 새 캐릭터에서 표준 End Bone이 없으면, 동일한 의미의 마지막 본을 사용하고 이 표에 추가한다.

## IK Retargeter 설정

1. Source·Target IK Rig에 위 체인을 같은 이름으로 만든다.
2. `FK Chains`에서 Target 체인과 Source 체인을 **동일한 이름끼리** 매핑한다.
   - 예: `LeftLeg → LeftLeg`, `RightArm → RightArm`
3. 새 리타게터를 만든 뒤에는 기존의 자동·수동 매핑이 남아 있는지 확인한다. 발이 손 동작을 따라가는 등 교차 동작이 보이면 FK 매핑을 직접 다시 지정한다.
4. Target Retarget Pose에서는 팔·척추·목의 **회전만** 보정한다. `root`와 `pelvis`의 위치를 움직이면 캐릭터가 공중에 뜰 수 있다.

## IK Goal과 Root Motion

- 손·발 IK Goal을 아직 만들지 않았다면 `Retarget IK Goals` 연산은 비활성화한다.
- In-Place 애니메이션만 리타기팅하는 초기 단계에서는 `Root Motion` 연산도 비활성화한다.
- Root Motion이 필요한 경우에만 다음을 명시한다.
  - Source Root: `root`
  - Target Root: `root`
  - Target Pelvis: `pelvis`
- 다리 길이 차이로 Target 발이 바닥에서 뜨면 `Pelvis Motion`의 Source/Target Pelvis를 모두 `pelvis`로 설정하고, `Floor Constraint Weight`를 `1.0`부터 시험한다.

## 검증 순서

1. Idle에서 발이 바닥에 닿고 머리·손이 정상 위치인지 확인한다.
2. Walk/Run에서 좌우 발이 서로의 동작을 따라가지 않는지 확인한다.
3. Jump에서 골반 높이와 착지 위치를 확인한다.
4. 활·총·양손무기처럼 쇄골 회전이 큰 동작에서 어깨와 팔꿈치를 확인한다.
5. 손을 쥐는 동작에서 손가락이 기본 펴기 자세에 남지 않는지 확인한다.
6. 체인·Retarget Pose를 바꾼 뒤에는 이미 Export한 Animation Sequence를 다시 Export한다. 기존 결과물은 자동 갱신되지 않는다.

## 완료 기준

- 양쪽 IK Rig의 Retarget Root가 `pelvis`다.
- 필수 7개 체인과, 가능한 경우 손가락 10개 체인이 동일 이름·범위로 존재한다.
- FK 매핑이 동일 이름끼리 정확히 연결되어 있다.
- Idle, 이동, 점프, 무기 동작, 손가락 동작을 최소 하나씩 미리 보기했다.

## 관련 문서

- [[Paragon 스켈레톤 그룹화 및 리타기팅 검증]]
- [[애니메이션 에셋 소스 및 도입 기준]]
