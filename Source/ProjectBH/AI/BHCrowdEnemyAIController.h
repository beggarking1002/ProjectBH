// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../Components/Combat/CombatEngagementSlotComponent.h"
#include "DetourCrowdAIController.h"
#include "BHCrowdEnemyAIController.generated.h"

class ABHHeroCharacter;
class ABHEnemy;
class UCombatEngagementSlotComponent;

UENUM(BlueprintType)
enum class EBHCombatSlotReleaseReason : uint8
{
	None,
	TargetChanged,
	TargetLost,
	UnPossessed,
	ReservationInvalid,
	AttackRangeMismatch,
	AttackStartFailed,
	MoveRequestFailed,
	PathFollowingFailed,
	Stalled,
	LeftEngagementRange,
	AttackRecoveryComplete,
	Staggered,
	Died
};

UENUM(BlueprintType)
enum class EBHCrowdAvoidanceQuality : uint8
{
	Low,
	Medium,
	Good,
	High
};

/** Traffic role used to coordinate simultaneous formation movement. */
UENUM(BlueprintType)
enum class EBHFormationMovementRole : uint8
{
	None,
	AttackIngress,
	WaitIngress,
	WaitIngressDeferred,
	HoldingTransit,
	StationaryHolding,
	HoldingYield,
	PendingTransit
};

/**
 * Server-authoritative controller for NavMesh enemy engagement.
 * Pursues the target directly outside combat range, then uses the shared
 * engagement component's formation slots inside combat range.
 *
 * Detour Crowd handles path-aware local avoidance. This controller selects a
 * player target, reserves one of that player's combat slots, and moves to the
 * reserved NavMesh position instead of moving directly to the player actor.
 */
UCLASS(Blueprintable)
class PROJECTBH_API ABHCrowdEnemyAIController : public ADetourCrowdAIController
{
	GENERATED_BODY()

public:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

	UFUNCTION(BlueprintPure, Category = "AI|Target")
	ABHHeroCharacter* GetCurrentTarget() const { return CurrentTarget; }

	EBHCombatSlotType GetCurrentCombatSlotType() const { return CurrentSlotType; }

	UFUNCTION(BlueprintPure, Category = "AI|Formation")
	EBHFormationMovementRole GetFormationMovementRole() const { return CurrentFormationMovementRole; }
	bool IsTemporaryHoldingYieldActive() const { return bHoldingYieldActive; }

	float GetSlotAcceptanceRadius() const { return SlotAcceptanceRadius; }
	bool IsEscapingCombatCore() const { return bEscapingCombatCore; }

	/** Releases the current reservation immediately and optionally delays the next request. */
	void ReleaseCombatSlot(EBHCombatSlotReleaseReason Reason, float ReacquireDelay = 0.0f);

	/** Queues a next-frame path refresh after the central slot manager changes this enemy's assignment. */
	void NotifyCombatSlotAssignmentChanged();

	/** Temporarily moves a stationary Holding owner aside without releasing its reservation. */
	bool RequestTemporaryHoldingYield(
		AActor* PassingRequester,
		const FVector& PassingPathDirection,
		const FVector& YieldGoal);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Crowd")
	bool bEnableCrowdObstacleAvoidance = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Crowd")
	bool bEnableCrowdSeparation = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Crowd", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CrowdSeparationWeight = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Crowd")
	EBHCrowdAvoidanceQuality CrowdAvoidanceQuality = EBHCrowdAvoidanceQuality::High;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Crowd")
	bool bEnableCrowdAnticipateTurns = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Crowd", meta = (ClampMin = "0.0", Units = "cm"))
	float CrowdCollisionQueryRange = 500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Crowd", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float CrowdAvoidanceRangeMultiplier = 1.2f;

	/** Target validation and stalled-move retry interval. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "0.1", Units = "s"))
	float TargetRefreshInterval = 0.5f;

	/** Per-controller interval variation so a spawned group does not repath on the same frame. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "0.0", Units = "s"))
	float TargetRefreshJitter = 0.15f;

	/** Enter the player-centered slot formation only after getting this close. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "0.0", Units = "cm"))
	float EngagementEnterRadius = 700.0f;

	/** Leave formation beyond this larger radius to avoid boundary oscillation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "0.0", Units = "cm"))
	float EngagementExitRadius = 900.0f;

	/** Keeps a valid distant corridor reservation inside formation range. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "0.0", Units = "cm"))
	float ReservedSlotExitMargin = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Movement Intent", meta = (ClampMin = "0.0", Units = "cm/s"))
	float PursuitSpeed = 500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Movement Intent", meta = (ClampMin = "0.0", Units = "cm/s"))
	float AttackIngressSpeed = 450.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Movement Intent", meta = (ClampMin = "0.0", Units = "cm/s"))
	float WaitMoveSpeed = 350.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Movement Intent", meta = (ClampMin = "0.0", Units = "cm/s"))
	float HoldingMoveSpeed = 320.0f;

	/** Speed used during initial approach and when a joined enemy must catch up to a distant slot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Movement Intent", meta = (ClampMin = "0.0", Units = "cm/s"))
	float FormationCatchUpMoveSpeed = 500.0f;

	/** Retry cadence while a Wait departure is held behind an active Attack ingress path. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Traffic Priority", meta = (ClampMin = "0.05", Units = "s"))
	float WaitIngressDeferRetryInterval = 0.2f;

	/** No-progress time required before asking one stationary Holding owner to yield. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Traffic Priority", meta = (ClampMin = "0.1", Units = "s"))
	float HoldingYieldTriggerDelay = 0.75f;

	/** Minimum time the Holding owner stays aside before pass detection can release it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Traffic Priority", meta = (ClampMin = "0.0", Units = "s"))
	float HoldingYieldMinimumDuration = 0.3f;

	/** Safety timeout after which the Holding owner returns even if pass detection fails. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Traffic Priority", meta = (ClampMin = "0.1", Units = "s"))
	float HoldingYieldMaximumDuration = 1.5f;

	/** Distance past the Holding owner's original cross-section that counts as a pass. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Traffic Priority", meta = (ClampMin = "0.0", Units = "cm"))
	float HoldingYieldPassDistance = 45.0f;

	/** Prevents one stalled requester from successively displacing multiple Holding owners. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Traffic Priority", meta = (ClampMin = "0.0", Units = "s"))
	float HoldingYieldRequestCooldown = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Traffic Priority", meta = (ClampMin = "1.0", Units = "cm"))
	float HoldingYieldAcceptanceRadius = 15.0f;

	/** Lead a moving player by this much while outside formation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "0.0", Units = "s"))
	float PursuitPredictionTime = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "1.0", Units = "cm"))
	float PursuitRepathDistance = 100.0f;

	/** Initial candidates charge to the formation edge without overrunning the player. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "0.0", Units = "cm"))
	float InitialChargeStopRadius = 450.0f;

	/** Prevents small distance changes from repeatedly moving this enemy between players. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "0.0", Units = "s"))
	float MinimumTargetHoldTime = 2.0f;

	/** A challenger must be at least this much closer before replacing a reachable held target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "0.0", Units = "cm"))
	float TargetSwitchDistanceAdvantage = 200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit")
	FVector TargetNavProjectionExtent = FVector(100.0f, 100.0f, 250.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float SlotAcceptanceRadius = 20.0f;

	/** A slot occupant settles only after path following has naturally braked below this speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SlotSettleSpeedThreshold = 5.0f;

	/** Tight acceptance used for ring-alignment waypoints before inward ingress. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float RingWaypointAcceptanceRadius = 30.0f;

	/** Reissue MoveTo after the moving player's reserved slot has shifted this far. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Engagement Slots", meta = (ClampMin = "1.0", Units = "cm"))
	float SlotRepathDistance = 80.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Engagement Slots", meta = (ClampMin = "1.0", Units = "cm"))
	float WaitSlotRepathDistance = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Engagement Slots", meta = (ClampMin = "1.0", Units = "cm"))
	float HoldingSlotRepathDistance = 250.0f;

	/** A joined enemy starts running after falling at least this far behind its reserved slot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float FormationCatchUpEnterDistance = 300.0f;

	/** Catch-up run ends only after getting this close, preventing gait flicker. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float FormationCatchUpExitDistance = 160.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery", meta = (ClampMin = "0.1", Units = "s"))
	float StuckTimeout = 2.0f;

	/** Hard watchdog for agents that keep jittering without getting closer to their move goal. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery", meta = (ClampMin = "0.1", Units = "s"))
	float NoProgressTimeout = 4.0f;

	/** Faster recovery while Run intent makes a no-progress crowd stall visually obvious. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery", meta = (ClampMin = "0.1", Units = "s"))
	float RunNoProgressTimeout = 1.5f;

	/** Attack owners must yield quickly even when their current gait is not Run. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery", meta = (ClampMin = "0.1", Units = "s"))
	float AttackNoProgressTimeout = 1.5f;

	/** A recovering Attack owner farther than this from its slot yields immediately. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery", meta = (ClampMin = "0.0", Units = "cm"))
	float RecoveringAttackSlotLeashDistance = 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery", meta = (ClampMin = "0.0", Units = "cm"))
	float StuckProgressDistance = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery", meta = (ClampMin = "0.0", Units = "cm/s"))
	float StuckSpeedThreshold = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery", meta = (ClampMin = "0.0", Units = "s"))
	float FailedSlotCooldown = 2.0f;

	/** Resolves symmetric Detour Crowd deadlocks when two live enemies already overlap. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery|Overlap")
	bool bEnableOverlapRecovery = true;

	/** Fraction of the two capsule radii sum treated as an actual overlap. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery|Overlap", meta = (ClampMin = "0.1", ClampMax = "1.5"))
	float OverlapDetectionScale = 0.9f;

	/** Enemies separated farther than this vertically are not part of the same overlap cluster. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery|Overlap", meta = (ClampMin = "0.0", Units = "cm"))
	float OverlapHeightTolerance = 100.0f;

	/** Minimum short NavMesh move used to break an overlap without abandoning the reserved slot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery|Overlap", meta = (ClampMin = "10.0", Units = "cm"))
	float OverlapEscapeDistance = 110.0f;

	/** Extra desired clearance beyond the two capsule radii. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery|Overlap", meta = (ClampMin = "0.0", Units = "cm"))
	float OverlapEscapePadding = 20.0f;

	/** Maximum time spent on one escape waypoint before normal pursuit resumes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery|Overlap", meta = (ClampMin = "0.1", Units = "s"))
	float OverlapEscapeDuration = 0.75f;

	/** Small retry gap that prevents escape-waypoint oscillation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery|Overlap", meta = (ClampMin = "0.0", Units = "s"))
	float OverlapRecoveryCooldown = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Debug|Crowd")
	bool bDrawCrowdDebug = true;

private:
	void ApplyCrowdFollowingSettings();
	void RefreshTargetAndMove();
	void RefreshAfterCombatSlotAssignmentChanged();
	ABHHeroCharacter* SelectTargetHero() const;
	bool IsHeroReachable(const ABHHeroCharacter* Hero) const;
	bool AcquireCombatSlot(ABHEnemy* ControlledEnemy);
	void RequestPursuitMove(ABHEnemy* ControlledEnemy, float AcceptanceRadius = 75.0f);
	void ApplyMovementIntent(ABHEnemy* ControlledEnemy, float MoveSpeed, bool bFaceTarget);
	float GetCurrentSlotMoveSpeed(const ABHEnemy* ControlledEnemy) const;
	void UpdateFormationCatchUpIntent(ABHEnemy* ControlledEnemy, float DistanceToSlot) const;
	float GetCurrentSlotRepathDistance() const;
	void ResetPursuitTracking();
	void ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason Reason, bool bTemporarilyExcludeReleasedSlot = false);
	void RecoverStalledCombatSlot(ABHEnemy* ControlledEnemy);
	bool TryStartOverlapRecovery(ABHEnemy* ControlledEnemy);
	bool UpdateOverlapRecovery(ABHEnemy* ControlledEnemy);
	void FinishOverlapRecovery(bool bStopEscapeMove);
	bool FindOverlapEscapeGoal(
		ABHEnemy* ControlledEnemy,
		FVector& OutEscapeGoal,
		ABHEnemy*& OutPrimaryBlocker) const;
	bool IsEnemyOverlapping(const ABHEnemy* FirstEnemy, const ABHEnemy* SecondEnemy) const;
	bool ShouldYieldOverlap(const ABHEnemy* ControlledEnemy, const ABHEnemy* OtherEnemy) const;
	int32 GetOverlapPriority(const ABHEnemy* Enemy) const;
	bool ShouldPreserveQueuePosition(EBHCombatSlotReleaseReason Reason) const;
	void RequestMoveToReservedSlot(const FVector& SlotLocation, float AcceptanceRadius);
	void ScheduleWaitIngressRetry();
	void ClearWaitIngressDeferral();
	bool UpdateTemporaryHoldingYield(ABHEnemy* ControlledEnemy);
	void FinishTemporaryHoldingYield(bool bStopYieldMove);
	bool UpdateStuckTracking(float DistanceToSlot, float Speed);
	void ResetStuckTracking();
	void DrawDebugStatus(const ABHEnemy* ControlledEnemy) const;

	UPROPERTY(Transient)
	TObjectPtr<ABHHeroCharacter> CurrentTarget;

	float TargetAcquiredTime = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UCombatEngagementSlotComponent> CurrentSlotComponent;

	EBHCombatSlotType CurrentSlotType = EBHCombatSlotType::None;
	int32 CurrentSlotIndex = INDEX_NONE;
	TWeakObjectPtr<AActor> CurrentSlotRequester;
	FVector LastRequestedSlotLocation = FVector::ZeroVector;
	bool bHasRequestedSlotMove = false;
	bool bForceSlotPathRefresh = false;
	EBHCombatMoveRouteStage CurrentMoveRouteStage = EBHCombatMoveRouteStage::Direct;
	EBHCombatMoveRouteStage LastRequestedRouteStage = EBHCombatMoveRouteStage::Direct;
	bool bEscapingCombatCore = false;
	EBHFormationMovementRole CurrentFormationMovementRole = EBHFormationMovementRole::None;
	bool bWaitIngressDeferred = false;
	TWeakObjectPtr<AActor> WaitIngressBlockingAttackRequester;
	bool bHoldingYieldActive = false;
	bool bHoldingYieldAtGoal = false;
	FVector HoldingYieldGoal = FVector::ZeroVector;
	FVector HoldingYieldOrigin = FVector::ZeroVector;
	FVector HoldingYieldPassingDirection = FVector::ZeroVector;
	TWeakObjectPtr<AActor> HoldingYieldPassingRequester;
	float HoldingYieldStartTime = 0.0f;
	float HoldingYieldRequestCooldownUntil = 0.0f;
	FAIRequestID HoldingYieldRequestID = FAIRequestID::InvalidRequest;

	EBHCombatSlotType TrackedSlotType = EBHCombatSlotType::None;
	int32 TrackedSlotIndex = INDEX_NONE;
	float ProgressReferenceDistance = 0.0f;
	float StuckElapsed = 0.0f;
	float NoProgressElapsed = 0.0f;
	float LastDistanceToSlot = 0.0f;
	bool bHasProgressSample = false;
	bool bIsChainingIntermediateRoute = false;

	EBHCombatSlotType ExcludedSlotType = EBHCombatSlotType::None;
	int32 ExcludedSlotIndex = INDEX_NONE;
	float ExcludedSlotUntil = 0.0f;
	float SlotRequestBlockedUntil = 0.0f;
	EBHCombatSlotReleaseReason LastReleaseReason = EBHCombatSlotReleaseReason::None;
	int32 LastObservedFormationRevision = INDEX_NONE;
	int32 ReformCount = 0;
	bool bIsReforming = false;
	bool bInEngagementFormation = false;
	bool bHasRequestedPursuitMove = false;
	FVector LastRequestedPursuitLocation = FVector::ZeroVector;
	float ResolvedTargetRefreshInterval = 0.5f;
	bool bSlotAssignmentRefreshQueued = false;
	bool bOverlapRecoveryActive = false;
	FVector OverlapRecoveryGoal = FVector::ZeroVector;
	TWeakObjectPtr<ABHEnemy> OverlapRecoveryBlocker;
	float OverlapRecoveryEndTime = 0.0f;
	float OverlapRecoveryCooldownUntil = 0.0f;
	FAIRequestID OverlapRecoveryRequestID = FAIRequestID::InvalidRequest;

	FTimerHandle TargetRefreshTimerHandle;
	FTimerHandle WaitIngressRetryTimerHandle;
};
