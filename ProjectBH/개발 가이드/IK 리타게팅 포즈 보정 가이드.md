# IK 리타게팅 포즈 보정 가이드

## 적용 상황

Source와 Target의 기준 포즈가 다를 때 사용한다. 현재 Mixamo X Bot은 T 포즈, Greystone은 A 포즈이므로 팔·어깨의 위치 또는 회전이 틀어지는 현상이 발생할 수 있다.

## 원칙

체인 구성 문제로 보기 전에 IK Retargeter의 **Retarget Pose**를 먼저 맞춘다. 체인 이름과 시작/종료 본은 공통 표준을 유지한다.

## X Bot → Greystone 보정 절차

1. `RTG_XBot_to_Greystone`을 연다.
2. 상단에서 `Edit Retarget Pose`를 켠다.
3. Source와 Target의 정면/측면을 비교한다.
4. **Target (Greystone)** 에 새 포즈를 만들고 `Pose_Greystone_To_T`처럼 이름을 붙인다.
5. Target의 `clavicle_l`, `upperarm_l`, `lowerarm_l`와 오른쪽 대응 본을 회전해 X Bot의 팔 벌어진 각도(T 포즈)에 맞춘다.
   - 좌우 상완이 수평에 가깝고, 팔꿈치 방향이 X Bot과 같은 전방을 향하게 한다.
   - 팔의 위치 이동은 하지 않고 회전만 사용한다.
6. Retarget Pose를 저장한 상태에서 Idle과 팔을 크게 쓰는 공격 애니메이션을 함께 미리 본다.
7. 손목이 여전히 돌아가면 `hand_l`/`hand_r`만 소폭 회전한다. 팔 전체가 틀어지면 손목이 아니라 clavicle과 upperarm부터 다시 조정한다.

## 점검 순서

1. Retarget Pose (T/A 포즈 차이)
2. Arm 체인 범위 (`clavicle`/`shoulder` → `hand`)
3. Chain Mapping이 같은 이름끼리 연결됐는지
4. Retargeter의 체인별 FK 회전 설정
5. 무기 소켓/무기 부착 오프셋

무기 위치가 어긋난 경우에도 먼저 맨손 자세가 올바른지 확인한 뒤, 마지막에 무기 소켓을 보정한다.
