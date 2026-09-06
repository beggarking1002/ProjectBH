# 전투 검증 계측 가이드

## 목적과 범위

이 작업은 현재 군중 전투의 실제 상태 전이, 예약 인계, 후보 선택을 관측하고 세션 종료 시 검토 가능한 로컬 증거를 남기는 것으로 이해하여 구현했다. 전투 알고리즘 개선이나 범용 텔레메트리가 목적이 아니다. 성공률·성능 수치를 미리 채우지 않는다. 빈 세션은 카운터 0과 헤더만 있는 CSV를 생성한다.

진입점은 `Source/ProjectBH/Diagnostics/BHCombatDiagnosticsSubsystem`이다. 서버 권위 Game/PIE 월드별로 생성하고, 기본 비활성 상태에서는 tick과 파일 출력을 하지 않는다. Shipping에서는 Subsystem 생성, 명령 등록, 계측 훅이 비활성화된다. 에디터 편집 월드는 측정하지 않는다.

## 콘솔 사용

Listen Server에서는 서버 PIE 창의 콘솔을 사용한다. 클라이언트 콘솔 요청은 거부하며 서버로 자동 전달하지 않는다. 서버 월드가 클라이언트 Enemy를 중복 수집하지 않는다. 플레이를 끝내기 **전에** Stop을 실행한다.

```text
bh.Diagnostics.TraceAlgorithms 0
bh.Diagnostics.Start Open_Normal_10
bh.Diagnostics.Status
bh.Diagnostics.Stop
```

상세 후보가 필요할 때는 다음과 같이 별도 세션을 시작한다.

```text
bh.Diagnostics.TraceAlgorithms 1
bh.Diagnostics.Start Corridor_Large_Bypass
bh.Diagnostics.Status
bh.Diagnostics.Stop
```

| 명령 | 동작 |
|---|---|
| `Start [ScenarioName]` | 이전 미저장 데이터를 버리고 새 SessionId·UTC 시작 시각·월드 시간을 기록한다. 이름 생략 시 Unnamed. |
| `Stop` | 진행 중인 Episode를 측정 경계에서 닫고 JSON/CSV 4개를 저장한다. 저장 실패 후 재실행하면 같은 파일에 재시도한다. |
| `Reset` | 기록을 중지하고 메모리 집계를 버린다. 파일은 만들지 않는다. |
| `Status` | 활성 여부, 시나리오, 게임 시간 경과, 주요 카운터, 최대 Vacancy, Trace 여부와 누락 행 수를 출력한다. |
| `TraceAlgorithms 0\|1` | 상세 후보 기록만 전환한다. 기본 false. Start/Reset은 이 설정을 유지한다. 세션 도중 전환해도 이전 Trace는 남는다. |

진단 기준은 Start 전에 설정한다.

```text
bh.Diagnostics.CoreEscapeTimeout 5
bh.Diagnostics.VacancyWarning 2
```

두 값은 초 단위이며 Start 때 고정된다. 전투 timeout·cooldown·이동 명령을 바꾸지 않는다. elapsed/duration은 UTC 벽시계가 아닌 `UWorld::GetTimeSeconds()` 기준이므로 일시정지와 시간 배율의 영향을 받는다. UTC 시각은 파일 식별·실행 기록용이다.

## 지표 정의

| 지표 | 정확한 의미 |
|---|---|
| FormationCandidateChangeCount | 공간 분석의 CandidateSpaceMode 값이 실제로 달라진 대입 횟수. 유효하지 않은 probe에서 CurrentSpaceMode로 되돌리는 경우도 포함한다. |
| FormationCommittedChangeCount | 히스테리시스 조건을 통과해 CurrentSpaceMode를 CandidateSpaceMode로 확정한 횟수. |
| FormationRevisionStart/End | 각 예약 컴포넌트의 첫 관측/마지막 revision. 여러 대상이 있으면 최상위 값은 null이고 FormationRevisionsByComponent를 사용한다. 서로 다른 대상의 revision은 합하지 않는다. |
| ReformCount | ReformReservations가 호출된 횟수. 개별 링 재배치·capacity 조정·대형 우선권 때문에 증가한 모든 revision과는 다르다. |
| HandoverAttempt/Success/FailureCount | ExecuteAttackWaitHandover 및 ExecuteCoreIntrusionHandover의 실행 호출/true/false 반환 횟수. 단순 후보 탐색·안정화 대기는 아직 Attempt가 아니다. |
| Average/MaxHandoverDuration | 호출 직전 기존 AttackHandoverElapsed 또는 CoreIntrusionStableElapsed. 안정화 대기 시간을 재사용하며 현재 세션 경과 시간으로 상한을 둔다. 함수 CPU 실행 시간이나 새 공격자의 첫 타격까지 걸린 시간이 아니다. 즉시 인계는 0일 수 있다. 성공·실패 실행 모두 분모에 들어간다. |
| CoreEscapeAttemptCount | ResolveCombatCoreEscapeGoal 실행으로 requester의 새 탈출 Episode를 시작한 횟수. 같은 탈출의 경로 재계산은 새 Attempt가 아니다. |
| CoreEscapeSuccessCount | 실제 경로 갱신에서 CoreEscape 이외 단계가 선택되고, 해당 전투 대상과의 2D 거리가 그 시점의 max(EffectiveCombatCoreRadius+1cm, CombatCoreEscapeExitRadius) 이상인 경우. 반경 통과 직후가 아니라 경로 갱신에서 완료를 확인한다. 진단 timeout 이전이어야 한다. |
| CoreEscapeFailureCount | 경로 후보 없음, 예약 해제, 다른 경로로 대체되었으나 종료 반경 미도달, 객체 소멸 또는 진단 timeout. |
| CoreEscapeTimeoutCount | 진행 중인 탈출이 진단 제한 시간을 넘은 Episode 수. Failure의 부분집합이며 같은 탈출을 반복 집계하지 않는다. |
| Average/MaxCoreEscapeDuration | 성공·실패 종료까지의 관측 시간. timeout은 최초 초과 관측 시점까지. timeout 뒤 늦게 벗어나더라도 성공으로 다시 세지 않는다. |
| CoreEscapeIncompleteCount | Stop 당시 아직 성공·실패 판정이 나지 않은 탈출 수. 성공률 분모에 넣기 전 관측 중단임을 구분해야 하며 완료 시간 평균에서는 제외한다. |
| PeakConcurrentAttackers | 동일 월드의 실제 Attacking 상태 최대 수. 상태 진입 이벤트와 주기 관측을 함께 사용한다. 예약 보유 수와 다르다. |
| PeakNonAttackSlotAttackers | Attacking인데 컨트롤러 SlotType이 Attack이 아닌 Enemy 수의 최댓값. 중앙 소유권 부재는 별도 InvariantViolation으로도 검증한다. |
| PeakSpacingViolationCount | 기존 UpdateDebugMetrics의 겹침 쌍 수를 대상별로 합산한 월드 최대값. Enemy 수가 아니다. |
| SpacingViolationEpisodeCount | 월드 내 겹침 쌍 합이 0에서 양수로 바뀐 횟수. 겹친 Enemy 쌍이 바뀌어도 전부 해소되기 전에는 같은 Episode다. |
| Total/MaxSpacingViolationDuration | 합이 양수인 Episode의 총/최대 관측 시간. Stop은 마지막 Episode의 관측 구간만 닫으며 End 이벤트 Detail을 SessionStopped로 표시한다. |
| ReleaseReasons | 컨트롤러가 실제로 보유 중이던 슬롯을 해제할 때의 기존 EBHCombatSlotReleaseReason별 횟수. 인계로 직접 강등되는 Stalled 경로도 포함한다. 단순 ReleaseSlot 호출 횟수와 다르다. |
| AttackVacancyEpisodeCount | 대상별로 실제 대기자·공격권 조건이 성립한 연속 관측 구간 수. 같은 대상에서 여러 슬롯이 비어도 하나다. |
| Average/MaxAttackVacancyDuration | 닫힌 대상별 Vacancy Episode 시간의 평균/최대. Stop으로 잘린 구간도 관측 기간으로 포함한다. |

Spacing의 기존 기준은 캡슐 반지름 합에서 5cm를 뺀 2D 거리다. 높이 차가 두 캡슐 반높이 중 큰 값보다 크면 같은 층으로 보지 않는다. 원래 디버그 계산을 재사용하며 계측 호출은 위반 선을 그리지 않는다.

### Vacancy 판정

`MaximumAttackVacancy`는 **이미 초 단위**다. `UpdateAttackVacancyTimers`가 누적한 빈 Attack 슬롯 시간을 디버그 화면에서 최댓값으로 표시한다. 공격 가능한 대기자 존재까지 보장하는 지표는 아니므로 유지하고 새 지표와 구분한다.

새 Vacancy는 초기 대형 구성 중이 아니고, 기존 `FindBestWaitAdmission(false/true)`가 실제 할당 가능한 Attack/Wait 쌍을 찾았으며, 후보 집합이 살아 있는 활성 풀 객체·Chasing 상태·해당 전투 대상 추적 상태인 Wait Enemy로 제한된 경우에만 시작한다. 이 관측용 필터는 선택 전에 적용하므로 회복 중인 우선 후보 하나가 다른 공격 가능한 대기자를 가리지 않는다. 일반 전투 호출은 필터를 전달하지 않으며 기존 선택 결과를 유지한다. 기존 도착 반경, 재진입 cooldown, 공격권 비용·크기 제한, 진입 경로, NavMesh, 공격 거리, Corridor 우선순위, fallback 지연을 그대로 사용한다. 단순 빈 슬롯이나 Holding/Pending 수는 근거로 쓰지 않는다.

주기 관측은 게임 시간 약 0.1초 간격이다. 관측 사이에 시작·종료한 짧은 Vacancy/Spacing Episode는 검출하지 못한다. 즉시 채워지는 공격권에서 0 Episode가 나오는 것은 가능한 정상 결과다. 긴 공백 검출이 목적이며 프레임 단위 latency 측정으로 사용하지 않는다.

## 성공·실패와 불변조건

인계 실패 사유는 InvalidOwnership, LargeOwnerProtected, CannotOccupy, SlotOrPathUnavailable, IntruderNotQueued다. 각 원래 false 반환 분기에 연결했다. 인계 성공은 예약 교체 함수가 true를 반환했음을 뜻하며 다음 공격 애니메이션 재생을 보장하지 않는다.

InvariantViolation은 같은 조건이 지속되는 동안 한 번 기록하고 해소 뒤 재발하면 다시 기록한다. Events.csv에는 ElapsedSeconds, ViolationType, 관련 객체 경로, FormationRevision, 슬롯/제한/상태값과 원인을 담는다.

| 검증 | 판정 및 한계 |
|---|---|
| 동시 공격자 초과 | 대상별 실제 Attacking 수가 GetActiveAttackSlotCount보다 큰 경우. 공격 진입 시점과 주기 상태 모두 확인한다. |
| 공격권 없는 공격 | 실제 Attacking 시 중앙의 활성 Attack 예약 소유권 또는 컨트롤러 Attack 상태가 없는 경우. |
| 대형 예약 충돌 | Large가 포함된 두 Attack 예약 위치의 2D 간격이 기존 exclusion radius 규칙을 위반한 경우. NavMesh 투영이 실패한 쌍은 거리 충돌로 단정하지 않는다. |
| CoreEscape 제한 시간 | 기존 게임플레이에 전용 제한 시간이 없어 진단 전용 기준으로 판정한다. 이동은 계속된다. |
| 공격 가능한 대기자가 있는데 장기 공백 | 위 Vacancy 조건이 진단 기준 시간 이상 연속 관측된 경우. |
| 한 슬롯의 복수 Enemy 소유권 | 중앙 배열 한 원소가 단일 weak pointer여서 같은 원소의 동시 복수 소유는 자료구조상 표현되지 않는다. 컨트롤러 캐시는 다음 프레임 갱신이 의도되어 있어 일시적 중복 주장만으로 위반을 만들지 않는다. 대신 동일 Enemy가 여러 중앙 슬롯에 중복 예약되는 DuplicateEnemyReservation을 검사한다. 이는 원래 조건과 다른 보조 검사다. |

대형이 혼자 비용 용량을 초과하는 예약은 기존 정책의 허용된 예외다. 공격권 비용과 실제 동시 공격자 수를 혼동해 위반을 생성하지 않는다. 예약 충돌 검사는 Attack의 exclusion radius를 대상으로 하며 모든 Wait/Holding 기하학이나 대형 wedge 자체의 완전성을 증명하지 않는다.

## 알고리즘 Trace

Trace는 기존 선택 함수가 실제 실행할 때만 생긴다. 그 함수가 매 갱신 실행되면 행 수도 많아질 수 있다. 관측을 위해 선택을 추가 실행하지 않는다. DecisionId는 월드 세션 내 단조 증가 번호다.

LargeReservation.csv는 대형 우선 Attack 예약 후보의 NavMesh·공격 거리·경로 검사와 실제 정책 판단을 기록한다. YieldCount는 정책이 선택 전에 계산한 양보 대상 수다. 선호 방향이 경로 점수보다 우선하는 기존 순서를 유지한다. FinalPathScore는 기존 PathLength + CongestionPenalty이며 진단에서 점수를 재구성하지 않는다. 조기에 거절된 후보의 미계산 값은 비운다. 투영 실패 위치와 아직 평가하지 않은 선호 방향도 빈칸이다. IsSelected는 최종 Plan에 채택된 슬롯만 1이다.

BypassDecision.csv의 실제 점수는 ApproachScore + ExitScore + RemainingArcCost다. 앞의 두 값에는 기존 경로 길이·혼잡 비용이 이미 들어 있다. 방향 잠금 성공 시 반대쪽 후보를 새로 계산하지 않으며 WasEvaluated=0, IsValid=0, NotEvaluatedDueToDirectionLock으로 표시한다. 이때 IsValid=0은 거절이 아니라 미평가다. 기존 잠금 실패 후 양방향을 다시 평가하면 같은 DecisionId에 실패한 잠금 행과 두 재평가 행이 남을 수 있다. 행 순서가 실제 평가 순서다. ParityTieBreak는 기존 1점 이내 동점 분기에서만 1이다.

## 파일과 표시

출력 위치는 `Saved/Diagnostics/ProjectBH/`다.

```text
BHCombat_<Scenario>_<UTC시작시각>_<SessionId>_Summary.json
BHCombat_<Scenario>_<UTC시작시각>_<SessionId>_Events.csv
BHCombat_<Scenario>_<UTC시작시각>_<SessionId>_LargeReservation.csv
BHCombat_<Scenario>_<UTC시작시각>_<SessionId>_BypassDecision.csv
```

JSON에는 시나리오·맵·NetMode·빌드 구성·시작/종료 UTC·게임 시간·기준값·카운터·실패 사유·대상별 revision을 저장한다. CSV는 UTF-8 BOM, CRLF와 CSV 인용부호 이스케이프를 사용한다. 이벤트·두 Trace 배열은 각각 100,000행 한도다. 초과 시 카운터는 계속 누적하지만 상세 행은 버리며 DroppedEventRows/DroppedTraceRows로 누락을 공개한다. 누락이 있는 CSV를 완전한 사건 목록으로 해석하지 않는다.

기존 슬롯 디버그 표시가 켜져 있으면 측정 중 하단 한 줄에 Recording, Scenario, Elapsed, 대형 변화, 인계/탈출 Success/Attempt, 위반 수, 최대 Vacancy를 표시한다. 별도 UI는 없다. 종료 시 저장하지 않으면 PIE 종료·맵 이동·Reset으로 메모리 기록이 사라질 수 있다.

## 게임플레이 영향과 검증 범위

계측 훅은 전투 함수의 반환값을 교체하지 않고 기존 분기·난수·정렬·경로 요청·예약 변경 순서를 유지한다. 정책의 선택 점수와 결과를 진단으로부터 읽는 역방향 의존성은 없다. 비활성 시 훅은 활성 Subsystem 확인 후 종료하며 파일 접근을 하지 않는다.

관측 비용 자체가 0이라는 뜻은 아니다. 기록 중 0.1초 샘플은 기존 디버그 겹침 검사와 const admission/NavMesh 질의를 수행하므로 CPU 비용이 있고 실행 시간에 영향을 줄 수 있다. 상세 Trace도 메모리를 사용한다. 게임플레이의 논리적 타이머를 수정하지 않는다는 것과 물리적 프레임 시간이 동일하다는 것은 다르다. 같은 전투 결과의 보장은 실제 고정 입력 비교 전까지 주장하지 않는다. FPS·P95는 계산하지 않으며 Unreal Insights 또는 CSV Profiler로 별도 측정한다.

## 권장 실제 시나리오

1. 빈 맵 또는 Enemy 없는 상태: Start/Status/Stop, 카운터 0과 Trace 헤더만 확인한다.
2. Open에서 Normal 5/10/20마리: 정지·원형 이동·급회전으로 대형 안정성, 동시 공격·Spacing을 확인한다.
3. Corridor 진입/이탈과 Pocket 경계 왕복: Candidate/Committed와 Revision 변화를 비교한다.
4. Normal 다수 + Large 1/2마리: 대형 양보 수·선호 방향·경로 점수·예약 충돌 Trace를 확인한다.
5. 공격권 소유자를 멀리 유도하거나 이동을 막기: 인계 시도/사유/기존 안정화 시간을 확인한다.
6. 플레이어 중심에 Enemy 진입: 정상 탈출, 경로 불가, 공격권 승격에 따른 직접 탈출, 진단 timeout을 각각 관찰한다. timeout 사례는 기준을 낮춰도 되지만 결과에 기준값을 함께 제시한다.
7. 공격권이 비었지만 대기자 없음·cooldown 중·입장 불가: Vacancy가 누적되지 않는지 확인한다. 실제 입장 가능한 대기자와 지속 공백이 있는 경우에만 Episode가 생기는지 확인한다.
8. Listen Server + 클라이언트 1개: 서버에서만 Start하고 클라이언트 Start가 거부되는지 확인한다. 하나의 서버 SessionId 출력만 보관한다.
9. 동일 스폰·입력·맵으로 비활성/활성/Trace 활성 실행을 비교한다. 외부 관찰한 공격권 소유자·우회 선택·공격 상태 전이 순서가 같은지 확인한다. 계측 로그만 서로 비교해 알고리즘 전체가 맞다고 결론내리지 않는다.

### 결과 확인 예시

| 준비·조작 | 확인할 자료 | 기대 결과 |
|---|---|---|
| Enemy 없는 맵에서 Trace 0, Start Empty, Status, Stop | Summary의 HandoverAttemptCount/CoreEscapeAttemptCount/AttackVacancyEpisodeCount와 두 후보 CSV | 세 카운터 0, 후보 CSV 헤더만 존재. 파일 4개 생성. |
| 공격권이 비었으나 대기 Enemy가 없는 상태를 세션 전체 유지 | AttackVacancyEpisodeCount | 0. 단순 빈 슬롯을 공백 Episode로 세면 계측 오류. |
| Core 탈출이 계속되는 동안 진단 제한 시간 초과 | CoreEscapeTimeoutCount, CoreEscapeFailuresByReason.DiagnosticTimeout, Events의 CoreEscapeTimeout | 같은 탈출에 timeout과 failure 각각 1, 이후 이동 지속. 이 실패 유도 실험에서 위반 기록은 기대 결과다. |
| Core 탈출 도중 Stop | CoreEscapeIncompleteCount와 Events의 CoreEscapeIncomplete | timeout 이전이면 미완료 1, 그 시도로 성공/실패 수와 완료 평균은 증가하지 않음. |
| 정상 공격권 소유 상태로 전투 | InvariantViolationsByType 및 Events의 ViolationType | 해당 소유권·용량 위반 0이 기대값. 발생 시 RelatedState와 실제 상태를 대조. |

인계 실패·겹침·Core 실패를 모든 시나리오에서 무조건 0으로 요구하지 않는다. 강제 정체나 경로 차단에서는 실패 사유가 의도와 일치하는지를 본다. 정상 전투 품질의 허용 상한은 실제 반복 실험으로 정해야 하며 이 문서가 임의의 합격 수치를 정하지 않는다.

## 자동 검증과 아직 필요한 증거

`ProjectBH.Diagnostics.EmptySessionCommands`는 별도 실제 Game 월드에서 기본 비활성, 명령 등록, Start/Status/Stop/Reset, JSON 파싱, 빈 카운터, Trace 기본/전환과 UTF-8 BOM을 검사한다. `Automation_Empty` 출력은 전투가 없는 자동 테스트 실행 자료이며 포트폴리오 전투 성공 실적으로 쓰지 않는다.

`ProjectBH.Diagnostics.LargePolicyObservation`은 고정 후보의 선호 방향 우선, 용량 거절, 기존 단독 초과 예외와 관측 유무의 선택 결과 일치를 확인한다. 이것만으로 실제 NavMesh·우회·Listen Server 전투를 검증했다고 볼 수 없다.

2026-09-06 검증 결과: Development Editor 빌드 성공, 자동 테스트 2개 성공(실패·테스트 경고 0), FeatureDevMap Listen Server 시작 후 Start/Status/Stop과 4개 파일 저장 성공, Shipping `-NoLink` 컴파일 성공. Listen 시작 smoke의 DurationSeconds는 0이며 클라이언트 접속·전투 검증을 뜻하지 않는다. 실행 로그와 자동 보고서는 `tmp/combat-diagnostics/`에 있고 Git에서 제외한다.

기존 환경 메시지로 MSVC 14.38 비권장 버전 경고가 있다. 실제 맵 실행에서는 기존 `FBHInputActionConfig::InputAction` 미초기화 오류 로그도 관찰했다. 이번 계측 변경에서 해당 입력 설정이나 컴파일러 설정은 수정하지 않았다. 실제 다중 Enemy 전투, Listen Server 중복 집계, 우회 Trace 선택 일치 및 계측 전후 전투 결과 비교는 위 시나리오의 실행 로그와 외부 관측이 필요하다.
