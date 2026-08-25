# Greystone Montage Notify 배치 절차

## 전제

`ANS Melee Hit Window`와 `AN Combo Branch`는 ProjectBH C++ 모듈이 컴파일된 뒤 Unreal Editor를 다시 열어야 Montage 메뉴에 나타난다. Live Coding이 켜져 있어 전체 C++ 빌드가 실패한 상태에서는 새 클래스가 보이지 않을 수 있다.

## `ANS Melee Hit Window` 배치

`ANS Melee Hit Window`는 **구간형 Notify State**다. 구간이 켜져 있는 동안에만 서버가 `Trace_Sword_Base`, `Trace_Sword_Mid`, `Trace_Sword_Tip`의 이전·현재 위치를 Sphere Sweep해 피해를 판정한다.

1. Content Browser에서 `/Game/Assets/Animation/AM_Knight`를 연다.
2. Montage 편집기의 타임라인에서 `Attack_A` 구간으로 이동한다.
3. Notify 트랙이 없으면 `Notifies` 영역을 우클릭해 Notify 트랙을 추가한다.
4. 검이 목표를 향해 실제로 움직이기 **직전**의 프레임을 우클릭한다.
5. 메뉴에서 `Add Notify State → ANS Melee Hit Window`를 선택한다.
6. 생성된 빨간 구간형 바의 오른쪽 끝을 드래그해, 검이 목표를 완전히 통과한 **직후** 프레임까지 늘린다.
7. 같은 방식으로 `Attack_B`, `Attack_C`에 각각 한 개씩 배치한다.

### 배치 기준

- Wind-up(검을 들어 올리는 준비 동작)은 포함하지 않는다.
- 검이 앞으로 휘두르기 시작하는 시점부터 칼날이 목표 위치를 지난 직후까지만 포함한다.
- `Recovery_A/B/C`에는 배치하지 않는다.
- 첫 설정은 약간 넓게 배치한 뒤, PIE Debug Draw에서 빈 타격 또는 누락을 보고 좁혀 조정한다.

## `AN Combo Branch` 배치

`AN Combo Branch`는 단발 Notify다. Recovery가 거의 끝난 시점에 서버가 입력 버퍼를 검사해 다음 Attack을 시작하거나 콤보를 끝낸다.

1. `Recovery_A`의 마지막 프레임 바로 앞을 우클릭한다.
2. `Add Notify → AN Combo Branch`를 선택한다.
3. `Recovery_B`, `Recovery_C`에도 같은 위치에 하나씩 배치한다.
4. Montage 섹션 연결은 `Attack_A → Recovery_A`, `Attack_B → Recovery_B`, `Attack_C → Recovery_C`만 유지하고, 각 Recovery의 다음 섹션은 비워 둔다.

## 보이지 않을 때

- `ANS Melee Hit Window`가 메뉴에 없으면 Unreal Editor와 PIE를 모두 종료하고 C++ 모듈을 전체 빌드한 뒤 에디터를 다시 연다.
- 클래스가 보인 뒤에도 피해가 없으면 Greystone 파생 Mesh에 `Trace_Sword_Base`, `Trace_Sword_Mid`, `Trace_Sword_Tip` Socket이 정확히 존재하는지 확인한다.
- Montage 미리보기에서는 서버 권한 피해가 발생하지 않는다. Notify의 구간 위치는 미리보기로 확인하고, 실제 피해는 PIE에서 확인한다.

## 관련 문서

- [[Greystone 검방패 전투 수직 슬라이스 작업 목록]]
- [[Greystone 3타 콤보 및 다중 스윕 기반 구현]]
