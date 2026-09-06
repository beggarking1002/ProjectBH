// Copyright ProjectBH. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BHCombatDiagnosticsSubsystem.generated.h"

class UCombatEngagementSlotComponent;
class ABHEnemy;

/** Local, opt-in evidence collector. Never feeds values back into combat decisions. */
UCLASS()
class PROJECTBH_API UBHCombatDiagnosticsSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual TStatId GetStatId() const override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
#if !UE_BUILD_SHIPPING
	static UBHCombatDiagnosticsSubsystem* Get(const UObject* Context);
	void Start(const FString& Scenario);
	bool Stop();
	void Reset();
	void Status() const;
	void SetTrace(bool bEnabled) { bTraceAlgorithms = bEnabled; }
	bool IsTracing() const { return bRecording && bTraceAlgorithms; }
	double Elapsed() const;
	FString DebugText() const;
	void Count(const FString& Name, const UObject* Subject, int32 Revision, const FString& Detail = FString());
	void Handover(const UObject* Subject, int32 Slot, int32 Revision, bool bSuccess, const TCHAR* Reason, double Duration);
	void BeginCore(const UObject* Enemy, const UCombatEngagementSlotComponent* Component);
	void EndCore(const UObject* Enemy, bool bSuccess, const FString& Reason);
	void Release(const UObject* Enemy, const UObject* Component, int32 Slot, int32 Revision, const FString& Reason);
	void AttackState(ABHEnemy* Enemy, bool bAttacking);
	uint64 NewDecision() { return IsTracing() ? ++DecisionSequence : 0; }
	void TraceLarge(uint64 Id, const UObject* Requester, int32 Slot, const FString& Position, bool bValid,
		const FString& Reason, int32 YieldCount, bool bPreferred, const FString& PathLength,
		const FString& Congestion, const FString& Score, bool bSelected, bool bPreferredEvaluated = true);
	void TraceBypass(uint64 Id, const UObject* Requester, int32 Direction, bool bEvaluated, bool bValid,
		const FString& Reason, const FString& Approach, const FString& Exit, const FString& Arc,
		const FString& Score, bool bLocked, bool bParity, bool bSelected, const FString& SelectionReason);
	void Violation(const FString& Key, const FString& Type, const UObject* Subject, int32 Revision, const FString& State, const FString& Detail);
	void Sample();
private:
	struct FDuration { int64 Count = 0; double Total = 0; double Max = 0; void Add(double Value); };
	struct FCore { TWeakObjectPtr<const UObject> Enemy; TWeakObjectPtr<const UCombatEngagementSlotComponent> Component; double Start = 0; bool bTimedOut = false; };
	struct FEpisode { double Start = -1; FDuration Durations; };
	struct FFormation { int32 Start = 0; int32 End = 0; };
	bool bRecording = false;
	bool bTraceAlgorithms = false;
	FString SessionId, ScenarioName;
	FDateTime StartTimestamp, EndTimestamp;
	double StartSeconds = 0, EndSeconds = 0;
	float SampleElapsed = 0;
	double CoreTimeoutSeconds = 5, VacancyWarningSeconds = 2;
	uint64 DecisionSequence = 0;
	int64 DroppedEventRows = 0, DroppedTraceRows = 0;
	TMap<FString, int64> Counters;
	TMap<FString, int64> HandoverFailures, CoreFailures, ReleaseReasons, ViolationReasons;
	TMap<FString, FFormation> Formations;
	TMap<FString, FCore> CoreAttempts;
	TSet<TWeakObjectPtr<ABHEnemy>> Attackers;
	TSet<FString> ActiveViolations, SampleViolations;
	TMap<FString, FEpisode> Vacancies;
	FEpisode Spacing;
	FDuration HandoverDurations, CoreDurations, VacancyDurations;
	TArray<FString> Events, LargeRows, BypassRows;
	void Event(const FString& Type, const UObject* Subject, int32 Revision, const FString& State, const FString& Detail);
	void UpdateEpisode(FEpisode& Episode, bool bActive, const TCHAR* Type, const UObject* Subject, int32 Revision, const FString& EndReason = TEXT("ObservedClear"));
	void CloseIntervals(const FString& Reason);
	void CheckCoreDeadline(const FString& Key, FCore& Core);
	void Append(TArray<FString>& Rows, const TArray<FString>& Cells, bool bTrace);
	bool Save();
#endif
};

#if !UE_BUILD_SHIPPING
#define BH_DIAGNOSTICS(Context, ...) do { if (auto* Diagnostics = UBHCombatDiagnosticsSubsystem::Get(Context)) { __VA_ARGS__; } } while (false)
#else
#define BH_DIAGNOSTICS(Context, ...) do {} while (false)
#endif
