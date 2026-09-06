// Copyright ProjectBH. All Rights Reserved.
#include "BHCombatDiagnosticsSubsystem.h"
#include "../Components/Combat/CombatEngagementSlotComponent.h"
#include "../AI/BHCrowdEnemyAIController.h"
#include "../Enemies/BHEnemy.h"
#include "../BHHeroCharacter.h"
#include "../ProjectBH.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

bool UBHCombatDiagnosticsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client && Super::ShouldCreateSubsystem(Outer);
#endif
}
TStatId UBHCombatDiagnosticsSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UBHCombatDiagnosticsSubsystem, STATGROUP_Tickables); }
bool UBHCombatDiagnosticsSubsystem::IsTickable() const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return bRecording && !IsTemplate() && GetWorld() && GetWorld()->GetNetMode() != NM_Client;
#endif
}
void UBHCombatDiagnosticsSubsystem::Tick(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	SampleElapsed += DeltaTime;
	if (SampleElapsed >= 0.1f) { SampleElapsed = 0; Sample(); }
#endif
}
#if !UE_BUILD_SHIPPING
namespace
{
constexpr int32 MaxRows = 100000;
FString Id(const UObject* Object) { return Object ? Object->GetPathName() : TEXT("None"); }
FString Number(double Value) { return FMath::IsFinite(Value) ? FString::Printf(TEXT("%.6f"), Value) : FString(); }
FString Bit(bool Value) { return Value ? TEXT("1") : TEXT("0"); }
UBHCombatDiagnosticsSubsystem* CommandWorld(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogProjectBH, Warning, TEXT("Diagnostics: run this command in the authority/server PIE console."));
		return nullptr;
	}
	return World->GetSubsystem<UBHCombatDiagnosticsSubsystem>();
}
FAutoConsoleCommandWithWorldAndArgs StartCommand(TEXT("bh.Diagnostics.Start"), TEXT("Start [ScenarioName]; discards previous unsaved session."), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { if (auto* D = CommandWorld(World)) D->Start(FString::Join(Args, TEXT(" "))); }));
FAutoConsoleCommandWithWorldAndArgs StopCommand(TEXT("bh.Diagnostics.Stop"), TEXT("Stop and save JSON/CSV."), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { if (auto* D = CommandWorld(World)) D->Stop(); }));
FAutoConsoleCommandWithWorldAndArgs ResetCommand(TEXT("bh.Diagnostics.Reset"), TEXT("Discard session and stop recording; no output."), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { if (auto* D = CommandWorld(World)) D->Reset(); }));
FAutoConsoleCommandWithWorldAndArgs StatusCommand(TEXT("bh.Diagnostics.Status"), TEXT("Show current recording status."), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { if (auto* D = CommandWorld(World)) D->Status(); }));
FAutoConsoleCommandWithWorldAndArgs TraceCommand(TEXT("bh.Diagnostics.TraceAlgorithms"), TEXT("TraceAlgorithms 0|1; default off."), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
	if (Args.Num() != 1 || (Args[0] != TEXT("0") && Args[0] != TEXT("1"))) { UE_LOG(LogProjectBH, Warning, TEXT("Usage: bh.Diagnostics.TraceAlgorithms 0|1")); return; }
	if (auto* D = CommandWorld(World)) { D->SetTrace(Args[0] == TEXT("1")); D->Status(); }
}));
TAutoConsoleVariable<float> CoreTimeout(TEXT("bh.Diagnostics.CoreEscapeTimeout"), 5.0f, TEXT("Diagnostic-only seconds; captured at Start. Does not cancel movement."));
TAutoConsoleVariable<float> VacancyWarning(TEXT("bh.Diagnostics.VacancyWarning"), 2.0f, TEXT("Diagnostic-only vacancy threshold in seconds; captured at Start."));
}
UBHCombatDiagnosticsSubsystem* UBHCombatDiagnosticsSubsystem::Get(const UObject* Context)
{
	UWorld* World = Context ? Context->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_Client) return nullptr;
	auto* D = World->GetSubsystem<UBHCombatDiagnosticsSubsystem>();
	return D && D->bRecording ? D : nullptr;
}
void UBHCombatDiagnosticsSubsystem::FDuration::Add(double Value) { ++Count; Value = FMath::Max(0.0, Value); Total += Value; Max = FMath::Max(Max, Value); }
double UBHCombatDiagnosticsSubsystem::Elapsed() const { return SessionId.IsEmpty() ? 0 : FMath::Max(0.0, (bRecording ? static_cast<double>(GetWorld()->GetTimeSeconds()) : EndSeconds) - StartSeconds); }
void UBHCombatDiagnosticsSubsystem::Reset()
{
	bRecording = false; SessionId.Reset(); Counters.Reset(); HandoverFailures.Reset(); CoreFailures.Reset(); ReleaseReasons.Reset(); ViolationReasons.Reset();
	Formations.Reset(); CoreAttempts.Reset(); Attackers.Reset(); ActiveViolations.Reset(); SampleViolations.Reset(); Vacancies.Reset();
	Spacing = {}; HandoverDurations = {}; CoreDurations = {}; VacancyDurations = {};
	Events.Reset(); LargeRows.Reset(); BypassRows.Reset(); DecisionSequence = 0; DroppedEventRows = 0; DroppedTraceRows = 0; SampleElapsed = 0;
}
void UBHCombatDiagnosticsSubsystem::Start(const FString& Scenario)
{
	Reset(); ScenarioName = Scenario.IsEmpty() ? TEXT("Unnamed") : Scenario.Left(128);
	SessionId = FGuid::NewGuid().ToString(EGuidFormats::Digits); StartTimestamp = FDateTime::UtcNow(); StartSeconds = GetWorld()->GetTimeSeconds();
	CoreTimeoutSeconds = FMath::Max(0.1f, CoreTimeout.GetValueOnGameThread()); VacancyWarningSeconds = FMath::Max(0.1f, VacancyWarning.GetValueOnGameThread());
	bRecording = true;
	for (TActorIterator<ABHEnemy> It(GetWorld()); It; ++It) if (It->IsPoolActive() && It->GetCombatState() == EBHEnemyCombatState::Attacking) Attackers.Add(*It);
	Sample(); Status();
}
bool UBHCombatDiagnosticsSubsystem::Stop()
{
	if (SessionId.IsEmpty()) { UE_LOG(LogProjectBH, Display, TEXT("Diagnostics: no session to save.")); return false; }
	if (bRecording) { Sample(); CloseIntervals(TEXT("SessionStopped")); EndSeconds = GetWorld()->GetTimeSeconds(); EndTimestamp = FDateTime::UtcNow(); bRecording = false; }
	return Save();
}
FString UBHCombatDiagnosticsSubsystem::DebugText() const
{
	double VacancyMax = VacancyDurations.Max;
	for (const auto& Pair : Vacancies) if (Pair.Value.Start >= 0) VacancyMax = FMath::Max(VacancyMax, Elapsed() - Pair.Value.Start);
	return FString::Printf(TEXT("Diagnostics %s [%s] %.1fs | F:%lld/%lld H:%lld/%lld Core:%lld/%lld Inv:%lld MaxVac:%.2fs Trace:%d"),
		bRecording ? TEXT("Recording") : TEXT("Off"), *ScenarioName, Elapsed(), Counters.FindRef(TEXT("FormationCandidateChangeCount")), Counters.FindRef(TEXT("FormationCommittedChangeCount")),
		Counters.FindRef(TEXT("HandoverSuccessCount")), Counters.FindRef(TEXT("HandoverAttemptCount")), Counters.FindRef(TEXT("CoreEscapeSuccessCount")), Counters.FindRef(TEXT("CoreEscapeAttemptCount")), Counters.FindRef(TEXT("InvariantViolationCount")), VacancyMax, bTraceAlgorithms);
}
void UBHCombatDiagnosticsSubsystem::Status() const { UE_LOG(LogProjectBH, Display, TEXT("%s | DroppedEvents:%lld DroppedTrace:%lld"), *DebugText(), DroppedEventRows, DroppedTraceRows); }
void UBHCombatDiagnosticsSubsystem::Append(TArray<FString>& Rows, const TArray<FString>& Cells, bool bTrace)
{
	if (Rows.Num() >= MaxRows) { if (bTrace) ++DroppedTraceRows; else ++DroppedEventRows; return; }
	TArray<FString> Escaped;
	for (FString Cell : Cells) { Cell.ReplaceInline(TEXT("\""), TEXT("\"\"")); Escaped.Add(TEXT("\"") + Cell + TEXT("\"")); }
	Rows.Add(FString::Join(Escaped, TEXT(",")));
}
void UBHCombatDiagnosticsSubsystem::Event(const FString& Type, const UObject* Subject, int32 Revision, const FString& State, const FString& Detail)
{
	FString ViolationType, RelatedState = State;
	if (Type == TEXT("InvariantViolation")) State.Split(TEXT(";"), &ViolationType, &RelatedState);
	Append(Events, { Number(Elapsed()), Type, ViolationType, Id(Subject), FString::FromInt(Revision), RelatedState, Detail }, false);
}
void UBHCombatDiagnosticsSubsystem::Count(const FString& Name, const UObject* Subject, int32 Revision, const FString& Detail)
{
	++Counters.FindOrAdd(Name);
	if (const auto* C = Cast<UCombatEngagementSlotComponent>(Subject))
	{
		FFormation* F = Formations.Find(Id(C));
		if (!F) { FFormation Initial; Initial.Start = Revision; Initial.End = Revision; F = &Formations.Add(Id(C), Initial); }
		F->End = Revision;
	}
	Event(Name, Subject, Revision, FString(), Detail);
}
void UBHCombatDiagnosticsSubsystem::Handover(const UObject* Subject, int32 Slot, int32 Revision, bool bSuccess, const TCHAR* Reason, double Duration)
{
	++Counters.FindOrAdd(TEXT("HandoverAttemptCount")); ++Counters.FindOrAdd(bSuccess ? TEXT("HandoverSuccessCount") : TEXT("HandoverFailureCount"));
	if (!bSuccess) ++HandoverFailures.FindOrAdd(Reason);
	Duration = FMath::Clamp(Duration, 0.0, Elapsed());
	HandoverDurations.Add(Duration);
	Event(TEXT("Handover"), Subject, Revision, FString::Printf(TEXT("Slot=%d;Success=%d;Duration=%.6f"), Slot, bSuccess, Duration), Reason);
}
void UBHCombatDiagnosticsSubsystem::BeginCore(const UObject* Enemy, const UCombatEngagementSlotComponent* Component)
{
	const FString Key = Id(Enemy);
	if (CoreAttempts.Contains(Key)) return;
	FCore& Core = CoreAttempts.Add(Key); Core.Enemy = Enemy; Core.Component = Component; Core.Start = Elapsed();
	Count(TEXT("CoreEscapeAttemptCount"), Enemy, Component->GetFormationRevision());
}
void UBHCombatDiagnosticsSubsystem::EndCore(const UObject* Enemy, bool bSuccess, const FString& Reason)
{
	FCore* Active = CoreAttempts.Find(Id(Enemy));
	if (!Active) return;
	CheckCoreDeadline(Id(Enemy), *Active);
	const FCore Core = *Active;
	CoreAttempts.Remove(Id(Enemy));
	const int32 Revision = Core.Component.IsValid() ? Core.Component->GetFormationRevision() : INDEX_NONE;
	if (!Core.bTimedOut)
	{
		++Counters.FindOrAdd(bSuccess ? TEXT("CoreEscapeSuccessCount") : TEXT("CoreEscapeFailureCount"));
		if (!bSuccess) ++CoreFailures.FindOrAdd(Reason);
		CoreDurations.Add(Elapsed() - Core.Start);
	}
	Event(TEXT("CoreEscapeEnd"), Enemy, Revision, FString::Printf(TEXT("Success=%d;TimedOut=%d;Duration=%.6f"), bSuccess && !Core.bTimedOut, Core.bTimedOut, Elapsed() - Core.Start), Reason);
}
void UBHCombatDiagnosticsSubsystem::Release(const UObject* Enemy, const UObject* Component, int32 Slot, int32 Revision, const FString& Reason)
{
	++ReleaseReasons.FindOrAdd(Reason); Event(TEXT("Release"), Enemy, Revision, FString::Printf(TEXT("Component=%s;Slot=%d"), *Id(Component), Slot), Reason);
	EndCore(Enemy, false, Reason);
}
void UBHCombatDiagnosticsSubsystem::AttackState(ABHEnemy* Enemy, bool bAttacking)
{
	if (bAttacking) Attackers.Add(Enemy); else Attackers.Remove(Enemy);
	for (auto It = Attackers.CreateIterator(); It; ++It) if (!It->IsValid() || !It->Get()->IsPoolActive()) It.RemoveCurrent();
	Counters.FindOrAdd(TEXT("PeakConcurrentAttackers")) = FMath::Max<int64>(Counters.FindRef(TEXT("PeakConcurrentAttackers")), Attackers.Num());
	// Entry-time checks use ownership arrays only; never run navigation in the state setter.
	if (bAttacking)
	{
		const auto* Controller = Cast<ABHCrowdEnemyAIController>(Enemy->GetController());
		const auto* C = Controller ? Controller->CurrentSlotComponent.Get() : nullptr;
		int32 Slot = INDEX_NONE;
		if (!C || Controller->CurrentSlotType != EBHCombatSlotType::Attack || !C->FindReservation(C->AttackReservations, Enemy, Slot) || Slot >= C->GetActiveAttackSlotCount())
			Violation(Id(Enemy) + TEXT(":AttackWithoutOwnership"), TEXT("AttackWithoutOwnership"), Enemy, C ? C->FormationRevision : INDEX_NONE, FString::Printf(TEXT("Slot=%d"), Slot), TEXT("Actual transition into Attacking without active Attack ownership."));
		int32 Concurrent = 0, NonAttack = 0;
		for (auto It = Attackers.CreateIterator(); It; ++It)
		{
			const ABHEnemy* Other = It->Get();
			if (!Other || !Other->IsPoolActive()) { It.RemoveCurrent(); continue; }
			const auto* OtherController = Cast<ABHCrowdEnemyAIController>(Other->GetController());
			if (!OtherController || OtherController->CurrentSlotType != EBHCombatSlotType::Attack) ++NonAttack;
			if (C && OtherController && OtherController->GetCurrentTarget() == C->GetOwner()) ++Concurrent;
		}
		Counters.FindOrAdd(TEXT("PeakNonAttackSlotAttackers")) = FMath::Max<int64>(Counters.FindRef(TEXT("PeakNonAttackSlotAttackers")), NonAttack);
		if (C && Concurrent > C->GetActiveAttackSlotCount()) Violation(Id(C) + TEXT(":AttackCapacity"), TEXT("ConcurrentAttackersExceeded"), C, C->FormationRevision,
			FString::Printf(TEXT("Attacking=%d;Capacity=%d"), Concurrent, C->GetActiveAttackSlotCount()), TEXT("Actual attack entry exceeded active reservation count."));
	}
}
void UBHCombatDiagnosticsSubsystem::Violation(const FString& Key, const FString& Type, const UObject* Subject, int32 Revision, const FString& State, const FString& Detail)
{
	SampleViolations.Add(Key);
	if (ActiveViolations.Contains(Key)) return;
	ActiveViolations.Add(Key); ++Counters.FindOrAdd(TEXT("InvariantViolationCount")); ++ViolationReasons.FindOrAdd(Type);
	Event(TEXT("InvariantViolation"), Subject, Revision, Type + TEXT(";") + State, Detail);
}
void UBHCombatDiagnosticsSubsystem::UpdateEpisode(FEpisode& Episode, bool bActive, const TCHAR* Type, const UObject* Subject, int32 Revision, const FString& EndReason)
{
	if (bActive && Episode.Start < 0) { Episode.Start = Elapsed(); ++Counters.FindOrAdd(FString(Type) + TEXT("EpisodeCount")); Event(FString(Type) + TEXT("Begin"), Subject, Revision, FString(), FString()); }
	else if (!bActive && Episode.Start >= 0)
	{
		const double Duration = Elapsed() - Episode.Start; Episode.Durations.Add(Duration);
		if (FCString::Strcmp(Type, TEXT("AttackVacancy")) == 0) VacancyDurations.Add(Duration);
		Event(FString(Type) + TEXT("End"), Subject, Revision, Number(Duration), EndReason); Episode.Start = -1;
	}
}
void UBHCombatDiagnosticsSubsystem::CheckCoreDeadline(const FString& Key, FCore& Core)
{
	if (Core.bTimedOut || Elapsed() - Core.Start < CoreTimeoutSeconds) return;
	Core.bTimedOut = true;
	++Counters.FindOrAdd(TEXT("CoreEscapeTimeoutCount")); ++Counters.FindOrAdd(TEXT("CoreEscapeFailureCount"));
	++CoreFailures.FindOrAdd(TEXT("DiagnosticTimeout")); CoreDurations.Add(Elapsed() - Core.Start);
	Violation(Key + TEXT(":CoreTimeout"), TEXT("CoreEscapeTimeout"), Core.Enemy.Get(), Core.Component.IsValid() ? Core.Component->GetFormationRevision() : INDEX_NONE,
		FString::Printf(TEXT("Duration=%.6f;DiagnosticLimit=%.3f"), Elapsed() - Core.Start, CoreTimeoutSeconds), TEXT("Diagnostic deadline exceeded; movement continues unchanged."));
}
void UBHCombatDiagnosticsSubsystem::CloseIntervals(const FString& Reason)
{
	UpdateEpisode(Spacing, false, TEXT("SpacingViolation"), nullptr, INDEX_NONE, Reason);
	for (auto& Pair : Vacancies) UpdateEpisode(Pair.Value, false, TEXT("AttackVacancy"), nullptr, INDEX_NONE, Reason + TEXT(";Component=") + Pair.Key);
	TArray<FString> Keys; CoreAttempts.GetKeys(Keys);
	for (const FString& Key : Keys)
	{
		const FCore Core = CoreAttempts[Key];
		if (!Core.bTimedOut) { ++Counters.FindOrAdd(TEXT("CoreEscapeIncompleteCount")); Event(TEXT("CoreEscapeIncomplete"), Core.Enemy.Get(), Core.Component.IsValid() ? Core.Component->GetFormationRevision() : INDEX_NONE, Number(Elapsed() - Core.Start), Reason); }
	}
	CoreAttempts.Reset();
}
void UBHCombatDiagnosticsSubsystem::TraceLarge(uint64 Decision, const UObject* Requester, int32 Slot, const FString& Position, bool bValid, const FString& Reason, int32 YieldCount, bool bPreferred, const FString& PathLength, const FString& Congestion, const FString& Score, bool bSelected, bool bPreferredEvaluated)
{
	if (!IsTracing() || !Decision) return;
	Append(LargeRows, { LexToString(Decision), Number(Elapsed()), Id(Requester), FString::FromInt(Slot), Position, Bit(bValid), Reason, YieldCount < 0 ? FString() : FString::FromInt(YieldCount), bPreferredEvaluated ? Bit(bPreferred) : FString(), PathLength, Congestion, Score, Bit(bSelected) }, true);
}
void UBHCombatDiagnosticsSubsystem::TraceBypass(uint64 Decision, const UObject* Requester, int32 Direction, bool bEvaluated, bool bValid, const FString& Reason, const FString& Approach, const FString& Exit, const FString& Arc, const FString& Score, bool bLocked, bool bParity, bool bSelected, const FString& SelectionReason)
{
	if (!IsTracing() || !Decision) return;
	Append(BypassRows, { LexToString(Decision), Number(Elapsed()), Id(Requester), Direction > 0 ? TEXT("Positive") : TEXT("Negative"), Bit(bEvaluated), Bit(bValid), Reason, Approach, Exit, Arc, Score, Bit(bLocked), Bit(bParity), Bit(bSelected), SelectionReason }, true);
}
#endif
#if !UE_BUILD_SHIPPING
void UBHCombatDiagnosticsSubsystem::Sample()
{
	if (!bRecording) return;
	SampleViolations.Reset();
	int32 TotalSpacing = 0, TotalAttackers = 0, TotalNonAttack = 0;
	TSet<FString> SeenComponents;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		auto* C = It->FindComponentByClass<UCombatEngagementSlotComponent>();
		if (!C || !It->HasAuthority()) continue;
		const FString ComponentId = Id(C); SeenComponents.Add(ComponentId);
		FFormation* Formation = Formations.Find(ComponentId);
		if (!Formation) { FFormation Initial; Initial.Start = C->FormationRevision; Initial.End = C->FormationRevision; Formation = &Formations.Add(ComponentId, Initial); }
		Formation->End = C->FormationRevision;
		C->UpdateDebugMetrics(false);
		TotalSpacing += C->CurrentSpacingViolationCount;
		TotalAttackers += C->CurrentAttackingEnemyCount;
		TotalNonAttack += C->CurrentNonAttackSlotAttackerCount;
		const int32 Capacity = C->GetActiveAttackSlotCount();
		if (C->CurrentAttackingEnemyCount > Capacity)
			Violation(ComponentId + TEXT(":AttackCapacity"), TEXT("ConcurrentAttackersExceeded"), C, C->FormationRevision, FString::Printf(TEXT("Attacking=%d;Capacity=%d"), C->CurrentAttackingEnemyCount, Capacity), TEXT("Actual Attacking states exceed active slot count."));
		// Authority has one weak owner per array entry. Check the independently cached
		// controller claims only while attacking; refresh can legitimately be deferred.
		for (TActorIterator<ABHEnemy> EnemyIt(GetWorld()); EnemyIt; ++EnemyIt)
		{
			ABHEnemy* Enemy = *EnemyIt;
			const auto* Controller = Cast<ABHCrowdEnemyAIController>(Enemy->GetController());
			if (!Controller || Controller->GetCurrentTarget() != It.operator->() || Enemy->GetCombatState() != EBHEnemyCombatState::Attacking) continue;
			int32 ReservedIndex = INDEX_NONE;
			const bool bOwns = C->FindReservation(C->AttackReservations, Enemy, ReservedIndex) && ReservedIndex < Capacity;
			if (!bOwns || Controller->GetCurrentCombatSlotType() != EBHCombatSlotType::Attack)
				Violation(Id(Enemy) + TEXT(":AttackWithoutOwnership"), TEXT("AttackWithoutOwnership"), Enemy, C->FormationRevision,
					FString::Printf(TEXT("ReservedIndex=%d;ControllerSlotType=%d;ControllerSlot=%d"), ReservedIndex, static_cast<int32>(Controller->CurrentSlotType), Controller->CurrentSlotIndex), TEXT("Attacking requires active authority Attack reservation and controller Attack state."));
		}
		TMap<const AActor*, FString> Owners;
		const auto CheckOwners = [&](const TArray<TWeakObjectPtr<AActor>>& Reservations, const TCHAR* Layer)
		{
			for (int32 Index = 0; Index < Reservations.Num(); ++Index)
			{
				const AActor* Owner = Reservations[Index].Get(); if (!Owner) continue;
				const FString Slot = FString::Printf(TEXT("%s:%d"), Layer, Index);
				if (const FString* Previous = Owners.Find(Owner)) Violation(ComponentId + Id(Owner) + TEXT(":DuplicateReservation"), TEXT("DuplicateEnemyReservation"), Owner, C->FormationRevision, *Previous + TEXT(";") + Slot, TEXT("One enemy appears in multiple authoritative reservation entries."));
				else Owners.Add(Owner, Slot);
			}
		};
		CheckOwners(C->AttackReservations, TEXT("Attack")); CheckOwners(C->WaitReservations, TEXT("Wait")); CheckOwners(C->HoldingReservations, TEXT("Holding"));
		for (int32 A = 0; A < C->AttackReservations.Num(); ++A)
		{
			const ABHEnemy* First = Cast<ABHEnemy>(C->AttackReservations[A].Get()); if (!First) continue;
			for (int32 B = A + 1; B < C->AttackReservations.Num(); ++B)
			{
				const ABHEnemy* Second = Cast<ABHEnemy>(C->AttackReservations[B].Get());
				if (!Second || (First->GetEnemySizeClass() != EBHEnemySizeClass::Large && Second->GetEnemySizeClass() != EBHEnemySizeClass::Large)) continue;
				FVector FirstPosition, SecondPosition;
				if (!C->GetSlotWorldLocation(EBHCombatSlotType::Attack, A, FirstPosition) || !C->GetSlotWorldLocation(EBHCombatSlotType::Attack, B, SecondPosition)) continue;
				const float Clearance = FMath::Max(First->GetAttackSlotExclusionRadius(), Second->GetAttackSlotExclusionRadius());
				if (Clearance > 0 && FVector::DistSquared2D(FirstPosition, SecondPosition) < FMath::Square(Clearance))
					Violation(ComponentId + FString::Printf(TEXT(":LargeCollision:%d:%d"), A, B), TEXT("LargeReservationConflict"), First, C->FormationRevision,
						FString::Printf(TEXT("Slots=%d,%d;Other=%s;Clearance=%.3f;Distance=%.3f"), A, B, *Id(Second), Clearance, FVector::Dist2D(FirstPosition, SecondPosition)), TEXT("Reserved Attack positions violate the existing exclusion-radius rule."));
			}
		}
		// Reuse the exact, const admission predicate. No new score, sorting of live
		// reservations, assignment, random number, cooldown, or movement is produced.
		int32 WaitIndex = INDEX_NONE, AttackIndex = INDEX_NONE;
		TSet<const AActor*> EligibleWaiters;
		for (const auto& Reservation : C->WaitReservations)
		{
			const auto* Waiting = Cast<ABHEnemy>(Reservation.Get());
			const auto* Controller = Waiting ? Cast<ABHCrowdEnemyAIController>(Waiting->GetController()) : nullptr;
			if (Waiting && Waiting->IsPoolActive() && Waiting->GetCombatState() == EBHEnemyCombatState::Chasing && Controller && Controller->GetCurrentTarget() == C->GetOwner()) EligibleWaiters.Add(Waiting);
		}
		// Filter all live candidates before selection: a recovering best candidate
		// must not hide another Chasing candidate that can fill the same vacancy.
		const bool bVacancy = !C->IsInitialFormationActive() && !EligibleWaiters.IsEmpty()
			&& (C->FindBestWaitAdmission(WaitIndex, AttackIndex, false, &EligibleWaiters)
				|| C->FindBestWaitAdmission(WaitIndex, AttackIndex, true, &EligibleWaiters));
		FEpisode& Vacancy = Vacancies.FindOrAdd(ComponentId);
		UpdateEpisode(Vacancy, bVacancy, TEXT("AttackVacancy"), C, C->FormationRevision);
		if (Vacancy.Start >= 0 && Elapsed() - Vacancy.Start >= VacancyWarningSeconds)
			Violation(ComponentId + TEXT(":Vacancy"), TEXT("ProlongedEligibleAttackVacancy"), C, C->FormationRevision, FString::Printf(TEXT("Slot=%d;Wait=%d;Duration=%.6f;Threshold=%.3f"), AttackIndex, WaitIndex, Elapsed() - Vacancy.Start, VacancyWarningSeconds), TEXT("Existing admission logic found a live Chasing Wait candidate and an assignable Attack reservation."));
	}
	for (auto& Pair : Vacancies) if (!SeenComponents.Contains(Pair.Key)) UpdateEpisode(Pair.Value, false, TEXT("AttackVacancy"), nullptr, INDEX_NONE);
	Counters.FindOrAdd(TEXT("PeakConcurrentAttackers")) = FMath::Max<int64>(Counters.FindRef(TEXT("PeakConcurrentAttackers")), TotalAttackers);
	Counters.FindOrAdd(TEXT("PeakNonAttackSlotAttackers")) = FMath::Max<int64>(Counters.FindRef(TEXT("PeakNonAttackSlotAttackers")), TotalNonAttack);
	Counters.FindOrAdd(TEXT("PeakSpacingViolationCount")) = FMath::Max<int64>(Counters.FindRef(TEXT("PeakSpacingViolationCount")), TotalSpacing);
	UpdateEpisode(Spacing, TotalSpacing > 0, TEXT("SpacingViolation"), nullptr, INDEX_NONE);
	TArray<FString> InvalidCore;
	for (auto& Pair : CoreAttempts)
	{
		FCore& Core = Pair.Value;
		if (!Core.Enemy.IsValid() || !Core.Component.IsValid()) { InvalidCore.Add(Pair.Key); continue; }
		CheckCoreDeadline(Pair.Key, Core);
		if (Core.bTimedOut) SampleViolations.Add(Pair.Key + TEXT(":CoreTimeout"));
	}
	for (const FString& Key : InvalidCore)
	{
		const FCore Core = CoreAttempts.FindAndRemoveChecked(Key);
		if (!Core.bTimedOut) { ++Counters.FindOrAdd(TEXT("CoreEscapeFailureCount")); ++CoreFailures.FindOrAdd(TEXT("ObjectDestroyed")); CoreDurations.Add(Elapsed() - Core.Start); Event(TEXT("CoreEscapeEnd"), nullptr, INDEX_NONE, Key, TEXT("ObjectDestroyed")); }
	}
	ActiveViolations = SampleViolations;
}

bool UBHCombatDiagnosticsSubsystem::Save()
{
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics/ProjectBH"));
	if (!IFileManager::Get().MakeDirectory(*Directory, true)) { UE_LOG(LogProjectBH, Error, TEXT("Diagnostics: cannot create %s"), *Directory); return false; }
	FString SafeScenario;
	for (TCHAR Char : ScenarioName) SafeScenario.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('-') ? Char : TEXT('_'));
	const FString Base = FPaths::Combine(Directory, FString::Printf(TEXT("BHCombat_%s_%s_%s"), *SafeScenario, *StartTimestamp.ToString(TEXT("%Y%m%dT%H%M%S")), *SessionId));
	const auto Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("SessionId"), SessionId); Root->SetStringField(TEXT("ScenarioName"), ScenarioName); Root->SetStringField(TEXT("MapName"), GetWorld()->GetMapName());
	Root->SetStringField(TEXT("NetMode"), GetWorld()->GetNetMode() == NM_ListenServer ? TEXT("ListenServer") : GetWorld()->GetNetMode() == NM_DedicatedServer ? TEXT("DedicatedServer") : TEXT("Standalone"));
	Root->SetStringField(TEXT("StartTimestamp"), StartTimestamp.ToIso8601()); Root->SetStringField(TEXT("EndTimestamp"), EndTimestamp.ToIso8601());
	Root->SetNumberField(TEXT("DurationSeconds"), Elapsed()); Root->SetStringField(TEXT("BuildConfiguration"), LexToString(FApp::GetBuildConfiguration()));
	Root->SetNumberField(TEXT("CoreEscapeDiagnosticTimeoutSeconds"), CoreTimeoutSeconds); Root->SetNumberField(TEXT("VacancyWarningSeconds"), VacancyWarningSeconds); Root->SetNumberField(TEXT("SampleIntervalSeconds"), 0.1);
	Root->SetNumberField(TEXT("DroppedEventRows"), DroppedEventRows); Root->SetNumberField(TEXT("DroppedTraceRows"), DroppedTraceRows); Root->SetBoolField(TEXT("TraceAlgorithmsAtStop"), bTraceAlgorithms);
	const TCHAR* Names[] = { TEXT("FormationCandidateChangeCount"), TEXT("FormationCommittedChangeCount"), TEXT("ReformCount"), TEXT("HandoverAttemptCount"), TEXT("HandoverSuccessCount"), TEXT("HandoverFailureCount"), TEXT("CoreEscapeAttemptCount"), TEXT("CoreEscapeSuccessCount"), TEXT("CoreEscapeFailureCount"), TEXT("CoreEscapeTimeoutCount"), TEXT("CoreEscapeIncompleteCount"), TEXT("PeakConcurrentAttackers"), TEXT("PeakNonAttackSlotAttackers"), TEXT("PeakSpacingViolationCount"), TEXT("SpacingViolationEpisodeCount"), TEXT("AttackVacancyEpisodeCount"), TEXT("InvariantViolationCount") };
	for (const TCHAR* Name : Names) Root->SetNumberField(Name, Counters.FindRef(Name));
	const auto SetDurations = [&](const TCHAR* Average, const TCHAR* Maximum, const FDuration& D) { Root->SetNumberField(Average, D.Count ? D.Total / D.Count : 0); Root->SetNumberField(Maximum, D.Max); };
	SetDurations(TEXT("AverageHandoverDuration"), TEXT("MaxHandoverDuration"), HandoverDurations);
	SetDurations(TEXT("AverageCoreEscapeDuration"), TEXT("MaxCoreEscapeDuration"), CoreDurations);
	SetDurations(TEXT("AverageAttackVacancyDuration"), TEXT("MaxAttackVacancyDuration"), VacancyDurations);
	Root->SetNumberField(TEXT("TotalSpacingViolationDuration"), Spacing.Durations.Total); Root->SetNumberField(TEXT("MaxSpacingViolationDuration"), Spacing.Durations.Max);
	const auto SetMap = [&](const TCHAR* Name, const TMap<FString, int64>& Map) { const auto Object = MakeShared<FJsonObject>(); for (const auto& P : Map) Object->SetNumberField(P.Key, P.Value); Root->SetObjectField(Name, Object); };
	SetMap(TEXT("HandoverFailuresByReason"), HandoverFailures); SetMap(TEXT("CoreEscapeFailuresByReason"), CoreFailures); SetMap(TEXT("ReleaseReasons"), ReleaseReasons); SetMap(TEXT("InvariantViolationsByType"), ViolationReasons);
	const auto Revisions = MakeShared<FJsonObject>();
	for (const auto& P : Formations) { const auto R = MakeShared<FJsonObject>(); R->SetNumberField(TEXT("FormationRevisionStart"), P.Value.Start); R->SetNumberField(TEXT("FormationRevisionEnd"), P.Value.End); Revisions->SetObjectField(P.Key, R); }
	Root->SetObjectField(TEXT("FormationRevisionsByComponent"), Revisions);
	// Revisions are local counters, never summed across combat targets.
	if (Formations.Num() == 1) { const auto& F = Formations.CreateConstIterator().Value(); Root->SetNumberField(TEXT("FormationRevisionStart"), F.Start); Root->SetNumberField(TEXT("FormationRevisionEnd"), F.End); }
	else { Root->SetField(TEXT("FormationRevisionStart"), MakeShared<FJsonValueNull>()); Root->SetField(TEXT("FormationRevisionEnd"), MakeShared<FJsonValueNull>()); }
	FString Json; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
	const auto Write = [&](const TCHAR* Suffix, const FString& Contents) { const FString File = Base + Suffix; const bool bOK = FFileHelper::SaveStringToFile(Contents, *File, FFileHelper::EEncodingOptions::ForceUTF8); if (!bOK) UE_LOG(LogProjectBH, Error, TEXT("Diagnostics: write failed: %s"), *File); return bOK; };
	bool bOK = Write(TEXT("_Summary.json"), Json);
	bOK &= Write(TEXT("_Events.csv"), TEXT("ElapsedSeconds,EventType,ViolationType,EnemyOrComponent,FormationRevision,RelatedState,Detail\r\n") + FString::Join(Events, TEXT("\r\n")) + TEXT("\r\n"));
	bOK &= Write(TEXT("_LargeReservation.csv"), TEXT("DecisionId,ElapsedSeconds,RequesterId,CandidateSlotId,CandidatePosition,IsValid,RejectedReason,YieldCount,PreferredSideMatched,PathLength,CongestionPenalty,FinalPathScore,IsSelected\r\n") + FString::Join(LargeRows, TEXT("\r\n")) + TEXT("\r\n"));
	bOK &= Write(TEXT("_BypassDecision.csv"), TEXT("DecisionId,ElapsedSeconds,EnemyId,Direction,WasEvaluated,IsValid,RejectedReason,ApproachScore,ExitScore,RemainingArcCost,FinalScore,WasPreviouslyLocked,ParityTieBreakApplied,IsSelected,SelectionReason\r\n") + FString::Join(BypassRows, TEXT("\r\n")) + TEXT("\r\n"));
	UE_LOG(LogProjectBH, Display, TEXT("Diagnostics: %s %s"), bOK ? TEXT("saved") : TEXT("FAILED saving; Stop can retry"), *Base); return bOK;
}
#endif
