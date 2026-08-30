// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatEngagementSlotComponent.generated.h"

UENUM(BlueprintType)
enum class EBHCombatSlotType : uint8
{
	None,
	Attack,
	Wait,
	Holding,
	Pending
};

/** Current movement stage used to reach a reserved combat slot. */
UENUM(BlueprintType)
enum class EBHCombatMoveRouteStage : uint8
{
	Direct,
	ApproachRing,
	AlignOnRing,
	Ingress
};

/** Coarse NavMesh space classification around the combat target. */
UENUM(BlueprintType)
enum class EBHCombatSpaceMode : uint8
{
	Open,
	Corridor,
	Pocket
};

/**
 * Server-authoritative reservation manager for combat positions around its owner.
 * Attack slots follow the owner directly, while outer formation rings may use a
 * delayed engagement anchor so the whole crowd does not mirror target movement.
 *
 * Slots are world-aligned rings around the owner. Their current positions are
 * projected onto NavMesh whenever an enemy requests or reads a reservation.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class PROJECTBH_API UCombatEngagementSlotComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatEngagementSlotComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Keeps an existing Attack reservation or promotes a Wait reservation when a reachable-range slot is free. */
	bool TryReserveAttackSlot(AActor* Requester, int32 ExcludedSlotIndex = INDEX_NONE);

	/** Keeps an existing Wait reservation or reserves one if the requester has no Attack slot. */
	bool TryReserveWaitSlot(AActor* Requester, int32 ExcludedSlotIndex = INDEX_NONE);

	/** Registers the requester once, then assigns Attack, Wait, Holding, or pending queue in that order. */
	bool RequestEngagementSlot(
		AActor* Requester,
		EBHCombatSlotType ExcludedSlotType = EBHCombatSlotType::None,
		int32 ExcludedSlotIndex = INDEX_NONE);

	/** Returns the requester's current slot and its latest NavMesh-projected world position. */
	bool GetReservedSlot(AActor* Requester, EBHCombatSlotType& OutSlotType, int32& OutSlotIndex, FVector& OutWorldLocation) const;

	/** Returns a unique projected overflow position for a queued requester without a ring reservation. */
	bool GetPendingWaitLocation(AActor* Requester, int32& OutPendingIndex, FVector& OutWorldLocation) const;

	/** Resolves the final slot or the next distributed ring-ingress waypoint. */
	bool GetMoveGoalForReservedSlot(
		AActor* Requester,
		EBHCombatSlotType SlotType,
		int32 SlotIndex,
		const FVector& FinalSlotLocation,
		EBHCombatMoveRouteStage PreviousRouteStage,
		FVector& OutMoveGoal,
		EBHCombatMoveRouteStage& OutRouteStage) const;

	/** Releases every slot owned by Requester and optionally preserves its central queue priority. */
	void ReleaseSlot(AActor* Requester, bool bPreserveQueuePosition = false);

	/** Atomically demotes a stalled Attack owner and promotes the best arrived Wait candidate. */
	bool HandleStalledAttackReservation(AActor* Requester, float AttackReentryCooldown);

	UFUNCTION(BlueprintPure, Category = "Combat|Engagement Slots")
	int32 GetAttackSlotCount() const { return AttackSlotCount; }

	UFUNCTION(BlueprintPure, Category = "Combat|Engagement Slots")
	int32 GetWaitSlotCount() const { return WaitSlotCount; }

	UFUNCTION(BlueprintPure, Category = "Combat|Engagement Slots")
	int32 GetHoldingSlotCount() const { return HoldingSlotCount; }

	uint64 GetQueueSequenceForRequester(AActor* Requester) const { return GetQueueSequence(Requester); }

	UFUNCTION(BlueprintPure, Category = "Debug|Engagement Slots")
	int32 GetCurrentSpacingViolationCount() const { return CurrentSpacingViolationCount; }

	UFUNCTION(BlueprintPure, Category = "Debug|Engagement Slots")
	int32 GetPeakSpacingViolationCount() const { return PeakSpacingViolationCount; }

	/** Increments whenever a large owner displacement triggers a coordinated ring reform. */
	UFUNCTION(BlueprintPure, Category = "Combat|Engagement Slots")
	int32 GetFormationRevision() const { return FormationRevision; }

	/** Initial candidates keep pursuing while this component gathers them for a fair assignment. */
	bool IsInitialFormationPending() const { return bInitialFormationActive; }

	/** Last stable NavMesh space classification. This is diagnostic only until formation rules consume it. */
	UFUNCTION(BlueprintPure, Category = "Combat|Space Analysis")
	EBHCombatSpaceMode GetCombatSpaceMode() const { return CurrentSpaceMode; }

	/** True after at least one complete NavMesh probe sample. */
	UFUNCTION(BlueprintPure, Category = "Combat|Space Analysis")
	bool HasValidCombatSpaceAnalysis() const { return bHasValidSpaceAnalysis; }

	/** Estimated traversable width perpendicular to the detected corridor axis. */
	UFUNCTION(BlueprintPure, Category = "Combat|Space Analysis")
	float GetEstimatedCorridorWidth() const { return EstimatedCorridorWidth; }

	/** World-space horizontal direction of the longest local traversable axis. */
	UFUNCTION(BlueprintPure, Category = "Combat|Space Analysis")
	FVector GetEstimatedCorridorAxis() const { return EstimatedCorridorAxis; }

	/** Attack slots currently usable by the active Open/Corridor/Pocket formation. */
	UFUNCTION(BlueprintPure, Category = "Combat|Space Analysis")
	int32 GetActiveAttackSlotCount() const;

	/** Stable Queue Sequence lane used by the active Corridor formation. */
	UFUNCTION(BlueprintPure, Category = "Combat|Space Analysis")
	int32 GetCorridorLaneForRequester(AActor* Requester) const;

	/** Stable axis side used by the active two-sided Corridor formation. */
	UFUNCTION(BlueprintPure, Category = "Combat|Space Analysis")
	int32 GetCorridorSideForRequester(AActor* Requester) const;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "1", UIMin = "1"))
	int32 AttackSlotCount = 5;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "1", UIMin = "1"))
	int32 WaitSlotCount = 8;

	/** Outer positions for alive enemies that cannot enter the 12-slot combat formation yet. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "1", UIMin = "1"))
	int32 HoldingSlotCount = 16;

	/** Kept inside the current 150-unit basic attack start range. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float AttackRingRadius = 125.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float WaitRingRadius = 250.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float HoldingRingRadius = 400.0f;

	/** First overflow radius used by queued enemies beyond Holding capacity. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Pending Queue", meta = (ClampMin = "0.0", Units = "cm"))
	float PendingRingRadius = 575.0f;

	/** Number of deterministic pending positions per overflow ring. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Pending Queue", meta = (ClampMin = "1", UIMin = "1"))
	int32 PendingSlotsPerRing = 24;

	/** Radial spacing between additional overflow rings. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Pending Queue", meta = (ClampMin = "0.0", Units = "cm"))
	float PendingRingSpacing = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Pending Queue", meta = (Units = "deg"))
	float PendingRingAngleOffset = 7.5f;

	/** Enemy routes cannot directly cross this player-centered radius. Kept below the Attack Ring radius at runtime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Approach Routing", meta = (ClampMin = "0.0", Units = "cm"))
	float CombatCoreRadius = 100.0f;

	/** Maximum angular step for one orbit waypoint around the Wait Ring. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Approach Routing", meta = (ClampMin = "5.0", ClampMax = "90.0", Units = "deg"))
	float OrbitWaypointAngleStep = 45.0f;

	/** Radial tolerance before an Enemy is treated as already aligned with a routing ring. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Approach Routing", meta = (ClampMin = "1.0", Units = "cm"))
	float OrbitRingAcceptanceRadius = 35.0f;

	/** Angular tolerance before an Enemy may leave an outer ring and enter its inner reserved slot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Approach Routing", meta = (ClampMin = "0.1", ClampMax = "15.0", Units = "deg"))
	float RingIngressAngleTolerance = 10.0f;

	/** Wait/Holding centers do not mirror every small player movement. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Anchor", meta = (ClampMin = "0.0", Units = "cm"))
	float EngagementAnchorDeadZone = 175.0f;

	/** Maximum follow speed once the player leaves the anchor dead zone. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Anchor", meta = (ClampMin = "0.0", Units = "cm/s"))
	float EngagementAnchorFollowSpeed = 350.0f;

	/** The owner must remain below this speed before the delayed anchor starts recentering. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Anchor", meta = (ClampMin = "0.0", Units = "cm/s"))
	float EngagementAnchorStopSpeedThreshold = 15.0f;

	/** Re-enter delayed following only after the owner clearly resumes movement. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Anchor", meta = (ClampMin = "0.0", Units = "cm/s"))
	float EngagementAnchorResumeSpeedThreshold = 40.0f;

	/** Prevents brief pauses during movement from immediately pulling the outer formation inward. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Anchor", meta = (ClampMin = "0.0", Units = "s"))
	float EngagementAnchorSettleDelay = 0.35f;

	/** Speed used to return Wait/Holding/Pending rings to a stationary player. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Anchor", meta = (ClampMin = "0.0", Units = "cm/s"))
	float EngagementAnchorRecenterSpeed = 200.0f;

	/** Snap the anchor to the player inside this distance to avoid a permanent residual offset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Anchor", meta = (ClampMin = "0.0", Units = "cm"))
	float EngagementAnchorRecenterSnapDistance = 7.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (Units = "deg"))
	float AttackRingAngleOffset = 45.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (Units = "deg"))
	float WaitRingAngleOffset = 22.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (Units = "deg"))
	float HoldingRingAngleOffset = 11.25f;

	/** Enables diagnostic NavMesh sampling around the owner. It does not change slots or Enemy behavior yet. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis")
	bool bEnableCombatSpaceAnalysis = true;

	/** Time between radial NavMesh samples. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.05", Units = "s"))
	float SpaceAnalysisInterval = 0.2f;

	/** Radial probe count. Runtime rounds this up to a multiple of four. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "8", ClampMax = "64", UIMin = "8", UIMax = "32"))
	int32 SpaceProbeCount = 16;

	/** Maximum NavMesh ray length in each probe direction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "100.0", Units = "cm"))
	float SpaceProbeDistance = 500.0f;

	/** Width at or below which an Open space may become a Corridor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "cm"))
	float CorridorEnterMaxWidth = 350.0f;

	/** Width at or above which a Corridor may return to Open. Must remain above the enter width. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "cm"))
	float CorridorExitMinWidth = 450.0f;

	/** Minimum longest-axis length required before the local area can be treated as a corridor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "cm"))
	float CorridorMinimumAxisLength = 600.0f;

	/** Longest-axis / width ratio required to enter Corridor mode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "1.0"))
	float CorridorEnterAspectRatio = 1.8f;

	/** Ratio at or below which a Corridor may return to Open. Must remain below the enter ratio. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "1.0"))
	float CorridorExitAspectRatio = 1.4f;

	/** Continuous Corridor evidence required before changing from Open. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "s"))
	float CorridorEnterDuration = 0.5f;

	/** Continuous Open evidence required before changing from Corridor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "s"))
	float CorridorExitDuration = 1.0f;

	/** Probe clearance required for a direction to belong to a broad Pocket opening. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "100.0", Units = "cm"))
	float PocketOpenProbeClearance = 350.0f;

	/** Nearby blocked-probe distance used to distinguish a wall or corner from a remote NavMesh edge. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "cm"))
	float PocketNearbyWallDistance = 200.0f;

	/** Minimum fraction of probes that must hit a nearby wall before entering Pocket mode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PocketEnterBlockedFraction = 0.2f;

	/** Lower nearby-wall threshold used while already in Pocket mode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PocketExitBlockedFraction = 0.1f;

	/** Broad contiguous opening required to distinguish a wall/corner pocket from a narrow corridor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", ClampMax = "360.0", Units = "deg"))
	float PocketEnterMinimumOpenArc = 90.0f;

	/** Lower open-arc threshold used while already in Pocket mode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", ClampMax = "360.0", Units = "deg"))
	float PocketExitMinimumOpenArc = 67.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "s"))
	float PocketEnterDuration = 0.4f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "s"))
	float PocketExitDuration = 0.6f;

	/** Converts ring slots to two axis-facing columns while stable Corridor mode is active. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation")
	bool bEnableCorridorFormation = true;

	/** Conservative half-width reserved for each Enemy center against corridor walls. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "1.0", Units = "cm"))
	float CorridorAgentRadius = 45.0f;

	/** Minimum center-to-center spacing used by corridor lanes and the Attack arc. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "1.0", Units = "cm"))
	float CorridorSlotSpacing = 95.0f;

	/** Longitudinal distance between successive rows in the pursuit column. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "1.0", Units = "cm"))
	float CorridorRowSpacing = 100.0f;

	/** Empty longitudinal separation between Attack, Wait, Holding, and Pending layers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "0.0", Units = "cm"))
	float CorridorLayerGap = 100.0f;

	/** Upper bound for side-by-side lanes regardless of measured width. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "1", ClampMax = "4", UIMin = "1", UIMax = "4"))
	int32 CorridorMaximumLaneCount = 2;

	/** Maximum half-angle used by each side's Attack arc in a corridor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "0.0", ClampMax = "89.0", Units = "deg"))
	float CorridorAttackArcHalfAngle = 70.0f;

	/** Smoothing applied when the detected axis turns while remaining inside a corridor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CorridorAxisFollowAlpha = 0.35f;

	/** Notify every reserved Enemy after the smoothed corridor direction turns this far. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "1.0", ClampMax = "90.0", Units = "deg"))
	float CorridorFormationRepathAngle = 15.0f;

	/** Maximum half-angle occupied by Pocket slots inside the detected open arc. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Pocket Formation", meta = (ClampMin = "10.0", ClampMax = "170.0", Units = "deg"))
	float PocketMaximumArcHalfAngle = 80.0f;

	/** Desired center-to-center spacing along each Pocket arc row. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Pocket Formation", meta = (ClampMin = "1.0", Units = "cm"))
	float PocketSlotSpacing = 95.0f;

	/** Radial spacing used when one Pocket arc cannot hold an entire layer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Pocket Formation", meta = (ClampMin = "1.0", Units = "cm"))
	float PocketRowSpacing = 100.0f;

	/** Smoothing applied while the detected Pocket opening direction changes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Pocket Formation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PocketDirectionFollowAlpha = 0.35f;

	/** Repath reserved enemies after the smoothed Pocket direction turns this far. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Pocket Formation", meta = (ClampMin = "1.0", ClampMax = "90.0", Units = "deg"))
	float PocketFormationRepathAngle = 15.0f;

	/** A queued enemy must be this close to its current ring slot before central promotion. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Queue", meta = (ClampMin = "0.0", Units = "cm"))
	float PromotionArrivalRadius = 60.0f;

	/** Initial arrivals wait provisionally, then receive a congestion-aware formation after this quiet period. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Queue", meta = (ClampMin = "0.0", Units = "s"))
	float InitialFormationSettleTime = 0.5f;

	/** Horizontal distance from a candidate Nav path used to count nearby crowd agents. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Admission", meta = (ClampMin = "0.0", Units = "cm"))
	float AdmissionCongestionRadius = 110.0f;

	/** Extra path cost added once for each crowd agent near a candidate path. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Admission", meta = (ClampMin = "0.0", Units = "cm"))
	float AdmissionCongestionPenaltyPerAgent = 200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots")
	FVector NavProjectionExtent = FVector(50.0f, 50.0f, 200.0f);

	/** Reassigns occupied slots within each ring after the owner moves this far from the last reform anchor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float ReformTriggerDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug|Engagement Slots")
	bool bDrawDebugSlots = true;

private:
	void InitializeSlots();
	void UpdateCombatSpaceAnalysis(float DeltaTime);
	void AnalyzeCombatSpace(float SampleDeltaTime);
	void HandleCombatSpaceModeChanged(EBHCombatSpaceMode PreviousMode);
	void UpdateCorridorFormationDirection();
	void UpdatePocketFormationDirection();
	void RefreshCorridorFormationCapacity();
	void RefreshPocketFormationCapacity();
	void ReconcileCorridorAttackReservations();
	bool ReconcilePocketAttackReservations();
	void RepackCorridorLayerReservations(
		TArray<TWeakObjectPtr<AActor>>& Reservations,
		EBHCombatSlotType SlotType,
		bool bNotifyMovedRequesters);
	void RepackAllCorridorQueueLayers(bool bNotifyMovedRequesters);
	void AssignCorridorSideIndices(bool bResetExistingAssignments);
	int32 GetLockedAttackReservationCount() const;
	int32 GetCorridorLaneIndex(AActor* Requester) const;
	int32 GetCorridorSideIndex(AActor* Requester) const;
	int32 GetCorridorQueueChannelIndex(AActor* Requester) const;
	int32 GetCorridorQueueChannelCount() const;
	int32 GetCorridorAttackSlotSideIndex(int32 AttackSlotIndex) const;
	int32 GetCorridorAttackSlotChannelIndex(int32 AttackSlotIndex) const;
	bool HasFreeCorridorLaneSlot(
		const TArray<TWeakObjectPtr<AActor>>& Reservations,
		int32 LaneIndex) const;
	bool FindCorridorLayerHead(
		const TArray<TWeakObjectPtr<AActor>>& Reservations,
		EBHCombatSlotType SlotType,
		int32 LaneIndex,
		bool bRequireArrival,
		int32& OutSlotIndex) const;
	bool FindCorridorPromotionCandidate(
		const TArray<TWeakObjectPtr<AActor>>& SourceReservations,
		EBHCombatSlotType SourceType,
		const TArray<TWeakObjectPtr<AActor>>& DestinationReservations,
		int32& OutSourceIndex) const;
	bool FindCorridorPendingCandidateForHolding(AActor*& OutRequester) const;
	bool FindCorridorWaitAdmissionForAttackSlot(
		int32 AttackSlotIndex,
		bool bAllowOtherLane,
		int32& OutWaitSlotIndex) const;
	FVector ResolveCorridorRearDirection(const FVector& UnsignedAxis) const;
	bool IsCorridorFormationActive() const;
	bool IsPocketFormationActive() const;
	int32 CalculateCorridorLaneCount(float CorridorWidth) const;
	int32 CalculateCorridorAttackSlotCount(float CorridorWidth) const;
	int32 CalculatePocketAttackSlotCount() const;
	float GetCorridorLayerStartDistance(EBHCombatSlotType SlotType) const;
	bool GetCorridorSlotWorldLocation(EBHCombatSlotType SlotType, int32 SlotIndex, FVector& OutWorldLocation) const;
	bool GetCorridorPendingWorldLocation(int32 PendingIndex, FVector& OutWorldLocation) const;
	bool GetPocketSlotWorldLocation(EBHCombatSlotType SlotType, int32 SlotIndex, FVector& OutWorldLocation) const;
	bool GetPocketPendingWorldLocation(int32 PendingIndex, FVector& OutWorldLocation) const;
	bool GetPocketFanWorldLocation(
		const FVector& Center,
		float BaseRadius,
		int32 SlotIndex,
		int32 SlotCount,
		bool bAllowMultipleRows,
		FVector& OutWorldLocation) const;
	void UpdateEngagementAnchor(float DeltaTime);
	void PruneInvalidReservations();
	void RefreshInitialFormationPhase();
	void FinalizeInitialFormationAssignments();
	void NotifyRequesterSlotChanged(AActor* Requester) const;
	void NotifyAllReservedRequestersSlotChanged() const;
	void RefreshPromotions();
	bool PromoteBestWaitReservationToAttack();
	bool PromoteOldestReservation(
		TArray<TWeakObjectPtr<AActor>>& SourceReservations,
		EBHCombatSlotType SourceType,
		EBHCombatSlotType DestinationType);
	bool AssignOldestPendingRequesterToHolding();
	bool FindOldestEligibleReservation(
		const TArray<TWeakObjectPtr<AActor>>& Reservations,
		EBHCombatSlotType SlotType,
		int32& OutReservationIndex) const;
	bool FindBestWaitAdmission(
		int32& OutWaitSlotIndex,
		int32& OutAttackSlotIndex) const;
	bool FindBestWaitAdmissionForAttackSlot(
		int32 AttackSlotIndex,
		int32& OutWaitSlotIndex) const;
	bool FindBestInitialAttackAssignment(
		AActor*& OutRequester,
		int32& OutAttackSlotIndex) const;
	bool FindOldestPendingRequester(AActor*& OutRequester) const;
	bool FindPendingRequesterIndex(AActor* Requester, int32& OutPendingIndex) const;
	bool IsRequesterReserved(AActor* Requester) const;
	bool HasAnyValidReservation(const TArray<TWeakObjectPtr<AActor>>& Reservations) const;
	void RegisterQueueRequester(AActor* Requester);
	void RemoveQueueRequester(AActor* Requester);
	uint64 GetQueueSequence(AActor* Requester) const;
	float GetAttackEligibleTime(AActor* Requester) const;
	void SetAttackEligibleTime(AActor* Requester, float EligibleTime);
	bool IsInitialFormationActive() const;
	void TryReformFormation();
	void ReformReservations();
	void ReformRingReservations(TArray<TWeakObjectPtr<AActor>>& Reservations, EBHCombatSlotType SlotType);
	float GetMaximumAttackSlotDistance(AActor* Requester) const;
	bool TryReserveSlot(AActor* Requester, EBHCombatSlotType SlotType, float MaxDistanceFromOwner = -1.0f, int32 ExcludedSlotIndex = INDEX_NONE);
	bool FindReservation(const TArray<TWeakObjectPtr<AActor>>& Reservations, AActor* Requester, int32& OutSlotIndex) const;
	bool GetSlotWorldLocation(EBHCombatSlotType SlotType, int32 SlotIndex, FVector& OutWorldLocation) const;
	bool DoesSegmentCrossCombatCore(const FVector& SegmentStart, const FVector& SegmentEnd) const;
	float GetEffectiveCombatCoreRadius() const;
	bool ProjectToNavigation(const FVector& DesiredLocation, FVector& OutProjectedLocation) const;
	bool GetNavigationPathScore(AActor* Requester, const FVector& Destination, float& OutPathScore) const;
	float CalculatePathCongestionPenalty(AActor* Requester, const TArray<FVector>& PathPoints) const;
	void UpdateDebugMetrics();
	void DrawDebugSlots() const;
	void DrawCombatSpaceAnalysisDebug() const;

	TArray<TWeakObjectPtr<AActor>> AttackReservations;
	TArray<TWeakObjectPtr<AActor>> WaitReservations;
	TArray<TWeakObjectPtr<AActor>> HoldingReservations;

	struct FEngagementQueueEntry
	{
		TWeakObjectPtr<AActor> Requester;
		uint64 Sequence = 0;
		float AttackEligibleTime = 0.0f;
		int32 CorridorSideIndex = INDEX_NONE;
	};
	TArray<FEngagementQueueEntry> EngagementQueue;
	uint64 NextQueueSequence = 1;
	float LastRequesterRegistrationTime = 0.0f;
	bool bInitialFormationActive = true;
	FVector EngagementAnchorLocation = FVector::ZeroVector;
	float EngagementAnchorStoppedElapsed = 0.0f;
	bool bEngagementAnchorRecentering = false;
	FVector LastReformOwnerLocation = FVector::ZeroVector;
	int32 FormationRevision = 0;
	int32 CurrentSpacingViolationCount = 0;
	int32 PeakSpacingViolationCount = 0;
	int32 CurrentAttackingEnemyCount = 0;
	int32 CurrentNonAttackSlotAttackerCount = 0;
	EBHCombatSpaceMode CurrentSpaceMode = EBHCombatSpaceMode::Open;
	EBHCombatSpaceMode CandidateSpaceMode = EBHCombatSpaceMode::Open;
	bool bHasValidSpaceAnalysis = false;
	float SpaceAnalysisElapsed = 0.0f;
	float SpaceModeTransitionElapsed = 0.0f;
	float EstimatedCorridorWidth = 0.0f;
	float EstimatedCorridorAxisLength = 0.0f;
	float EstimatedCorridorAspectRatio = 0.0f;
	FVector EstimatedCorridorAxis = FVector::ForwardVector;
	float EstimatedPocketOpenArc = 0.0f;
	float EstimatedPocketBlockedFraction = 0.0f;
	FVector EstimatedPocketOpenDirection = FVector::ForwardVector;
	FVector CorridorFormationRearDirection = -FVector::ForwardVector;
	FVector LastNotifiedCorridorDirection = -FVector::ForwardVector;
	FVector PocketFormationDirection = FVector::ForwardVector;
	FVector LastNotifiedPocketDirection = FVector::ForwardVector;
	FVector SpaceProbeOrigin = FVector::ZeroVector;
	int32 ActiveCorridorLaneCount = 1;
	int32 ActiveCorridorAttackSlotCount = 1;
	int32 DesiredCorridorAttackSlotCount = 1;
	int32 ActivePocketAttackSlotCount = 1;
	int32 DesiredPocketAttackSlotCount = 1;
	int32 CorridorAxisProbeIndex = INDEX_NONE;
	int32 CorridorWidthProbeIndex = INDEX_NONE;
	TArray<FVector> SpaceProbeEndpoints;
	TArray<uint8> SpaceProbeBlocked;
};
