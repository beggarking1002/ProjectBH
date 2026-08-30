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
	Ingress,
	BypassCorePositive,
	BypassCoreNegative
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

	/** Extra distance kept between the dynamic Corridor bypass ring and the Combat Core. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Approach Routing", meta = (ClampMin = "0.0", Units = "cm"))
	float CombatCoreBypassPadding = 45.0f;

	/** Maximum 2D NavMesh projection offset accepted for a dynamic Corridor bypass waypoint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Approach Routing", meta = (ClampMin = "0.0", Units = "cm"))
	float CombatCoreBypassProjectionTolerance = 35.0f;

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

	/** Distance from the player to the forward and rear cross-sections used to detect a corridor mouth. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "50.0", Units = "cm"))
	float CorridorMouthSampleDistance = 250.0f;

	/** Maximum half-width inspected at each corridor-mouth cross-section. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "100.0", Units = "cm"))
	float CorridorMouthProbeHalfWidth = 350.0f;

	/** A sampled cross-section must be at least this wide before it can describe an opening. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "cm"))
	float CorridorMouthMinimumOpenWidth = 500.0f;

	/** Required sampled-width / local-width growth before the opening is treated as a mouth. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "1.0"))
	float CorridorMouthExpansionRatio = 1.4f;

	/** Mouth evidence exits Corridor faster than the normal noisy-space hysteresis. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "s"))
	float CorridorMouthExitDuration = 0.3f;

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

	/** Enter a corridor-edge Pocket when an opposite probe pair has a boundary this close. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "cm"))
	float PocketCorridorEdgeEnterDistance = 100.0f;

	/** Keep a corridor-edge Pocket until its nearby boundary is farther than this distance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "cm"))
	float PocketCorridorEdgeExitDistance = 130.0f;

	/** Required far-minus-near clearance when first entering a corridor-edge Pocket. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "cm"))
	float PocketCorridorEdgeEnterClearanceDifference = 100.0f;

	/** Lower far-minus-near clearance retained while already in a corridor-edge Pocket. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Space Analysis", meta = (ClampMin = "0.0", Units = "cm"))
	float PocketCorridorEdgeExitClearanceDifference = 60.0f;

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

	/** Maximum lanes generated independently for each side-row from its measured NavMesh width. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "1", ClampMax = "8", UIMin = "1", UIMax = "4"))
	int32 CorridorRowMaximumLaneCount = 4;

	/** Maximum half-width inspected when building one dynamic Corridor row. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "100.0", Units = "cm"))
	float CorridorRowProbeHalfWidth = 350.0f;

	/** A changed row topology must remain stable this long before slot indices are repacked. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "0.0", Units = "s"))
	float CorridorRowLayoutCommitDelay = 0.35f;

	/** Maximum distance allowed between a dynamic row point and its NavMesh projection. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "0.0", Units = "cm"))
	float CorridorRowProjectionTolerance = 35.0f;

	/** Empty longitudinal separation between Attack, Wait, Holding, and Pending layers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "0.0", Units = "cm"))
	float CorridorLayerGap = 100.0f;

	/** Upper bound for side-by-side lanes regardless of measured width. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "1", ClampMax = "4", UIMin = "1", UIMax = "4"))
	int32 CorridorMaximumLaneCount = 2;

	/** Number of evenly spaced 360-degree Attack positions inspected around the player. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "8", ClampMax = "32"))
	int32 CorridorAttackCandidateCount = 16;

	/** Minimum center-to-center distance retained between selected Corridor Attack candidates. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "1.0", Units = "cm"))
	float CorridorAttackMinimumSpacing = 90.0f;

	/** Number of radial NavMesh clearance rays tested around each Attack candidate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "4", ClampMax = "16"))
	int32 CorridorAttackClearanceProbeCount = 8;

	/** Maximum distance allowed between a desired Corridor Attack point and its NavMesh projection. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "0.0", Units = "cm"))
	float CorridorAttackProjectionTolerance = 35.0f;

	/** A changed side-capacity layout must remain stable this long before live reservations are rebuilt. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Corridor Formation", meta = (ClampMin = "0.0", Units = "s"))
	float CorridorCapacityCommitDelay = 0.3f;

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
	struct FCorridorAttackSlot
	{
		FVector WorldLocation = FVector::ZeroVector;
		int32 SampleIndex = INDEX_NONE;
		int32 SideIndex = INDEX_NONE;
		float AxisAlignment = 0.0f;
	};

	struct FCorridorRowSlot
	{
		FVector WorldLocation = FVector::ZeroVector;
		int32 SideIndex = INDEX_NONE;
		int32 RowIndex = INDEX_NONE;
		int32 LaneIndex = INDEX_NONE;
		int32 LaneCount = 0;
	};

	void InitializeSlots();
	void UpdateCombatSpaceAnalysis(float DeltaTime);
	void AnalyzeCombatSpace(float SampleDeltaTime);
	bool MeasureCorridorCrossSection(
		const FVector& DesiredCenter,
		const FVector& AxisDirection,
		float ProbeHalfWidth,
		float& OutWidth,
		FVector& OutCenter,
		FVector& OutSide0,
		FVector& OutSide1) const;
	void HandleCombatSpaceModeChanged(EBHCombatSpaceMode PreviousMode);
	void UpdateCorridorFormationDirection();
	void UpdatePocketFormationDirection();
	void RefreshCorridorFormationCapacity(float DeltaTime);
	void RefreshCorridorRowLayout(float DeltaTime);
	void RefreshPocketFormationCapacity();
	void ReconcileCorridorAttackReservations();
	bool ReconcilePocketAttackReservations();
	void RepackCorridorLayerReservations(
		TArray<TWeakObjectPtr<AActor>>& Reservations,
		EBHCombatSlotType SlotType,
		bool bNotifyMovedRequesters);
	void RepackAllCorridorQueueLayers(bool bNotifyMovedRequesters);
	bool BuildCorridorRowLayouts(
		TArray<FCorridorRowSlot>& OutWaitLayout,
		TArray<FCorridorRowSlot>& OutHoldingLayout,
		float& OutPendingStartDistance) const;
	bool BuildCorridorLayerLayout(
		EBHCombatSlotType SlotType,
		float StartDistance,
		int32 RequiredSlotCount,
		TArray<FCorridorRowSlot>& OutLayout,
		float& OutLastRowDistance) const;
	bool BuildCorridorRowSlots(
		int32 SideIndex,
		int32 RowIndex,
		float LongitudinalDistance,
		TArray<FCorridorRowSlot>& OutRowSlots) const;
	bool AreCorridorRowLayoutsEquivalent(
		const TArray<FCorridorRowSlot>& LayoutA,
		const TArray<FCorridorRowSlot>& LayoutB) const;
	bool GetCorridorDynamicSlotWorldLocation(
		EBHCombatSlotType SlotType,
		int32 SlotIndex,
		FVector& OutWorldLocation) const;
	void AssignCorridorSideIndices(bool bResetExistingAssignments);
	int32 GetLockedAttackReservationCount() const;
	int32 GetCorridorLaneIndex(AActor* Requester) const;
	int32 GetCorridorSideIndex(AActor* Requester) const;
	int32 GetCorridorQueueChannelIndex(AActor* Requester) const;
	int32 GetCorridorAttackSlotSideIndex(int32 AttackSlotIndex) const;
	int32 GetCorridorAttackSlotChannelIndex(int32 AttackSlotIndex) const;
	void UpdateCorridorSideForAttackReservation(AActor* Requester, int32 AttackSlotIndex);
	bool FindCorridorWaitAdmissionForAttackSlot(
		int32 AttackSlotIndex,
		bool bAllowOtherSide,
		int32& OutWaitSlotIndex) const;
	FVector ResolveCorridorRearDirection(const FVector& UnsignedAxis) const;
	bool IsCorridorFormationActive() const;
	bool IsPocketFormationActive() const;
	int32 CalculateCorridorLaneCount(float CorridorWidth) const;
	bool BuildCorridorAttackCandidateLayout(TArray<FCorridorAttackSlot>& OutLayout);
	bool GetCorridorAttackCandidateDesiredLocation(
		int32 SampleIndex,
		FVector& OutDesiredLocation,
		FVector& OutDirection) const;
	bool HasCorridorAttackCandidateClearance(
		const FVector& CandidateLocation) const;
	bool AreCorridorAttackLayoutsEquivalent(
		const TArray<FCorridorAttackSlot>& LayoutA,
		const TArray<FCorridorAttackSlot>& LayoutB) const;
	void RefreshCorridorAttackLayoutWorldLocations(
		TArray<FCorridorAttackSlot>& Layout) const;
	void CommitCorridorAttackLayout(TArray<FCorridorAttackSlot>&& NewLayout);
	void RefreshCorridorAttackSideCounts();
	int32 CalculatePocketAttackSlotCount() const;
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
	bool DoesSegmentCrossCombatRadius(
		const FVector& SegmentStart,
		const FVector& SegmentEnd,
		float Radius) const;
	float GetEffectiveCombatCoreRadius() const;
	bool ResolveCorridorCombatCoreBypassGoal(
		AActor* Requester,
		const FVector& FinalSlotLocation,
		EBHCombatMoveRouteStage PreviousRouteStage,
		FVector& OutMoveGoal,
		EBHCombatMoveRouteStage& OutRouteStage) const;
	bool ProjectToNavigation(const FVector& DesiredLocation, FVector& OutProjectedLocation) const;
	bool GetNavigationPathScore(AActor* Requester, const FVector& Destination, float& OutPathScore) const;
	bool GetNavigationPathScoreBetween(
		AActor* Requester,
		const FVector& Start,
		const FVector& Destination,
		float& OutPathScore,
		bool bRejectCombatCoreCrossing = false) const;
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
	float EstimatedCorridorEdgeNearDistance = 0.0f;
	float EstimatedCorridorEdgeClearanceDifference = 0.0f;
	bool bCorridorEdgePocketActive = false;
	bool bCorridorMouthDetected = false;
	bool bCorridorForwardCrossSectionValid = false;
	bool bCorridorRearCrossSectionValid = false;
	float EstimatedCorridorForwardWidth = 0.0f;
	float EstimatedCorridorRearWidth = 0.0f;
	FVector CorridorForwardCrossSectionCenter = FVector::ZeroVector;
	FVector CorridorForwardCrossSectionSide0 = FVector::ZeroVector;
	FVector CorridorForwardCrossSectionSide1 = FVector::ZeroVector;
	FVector CorridorRearCrossSectionCenter = FVector::ZeroVector;
	FVector CorridorRearCrossSectionSide0 = FVector::ZeroVector;
	FVector CorridorRearCrossSectionSide1 = FVector::ZeroVector;
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
	int32 ActiveCorridorSide0AttackSlotCount = 1;
	int32 ActiveCorridorSide1AttackSlotCount = 0;
	int32 DesiredCorridorSide0AttackSlotCount = 1;
	int32 DesiredCorridorSide1AttackSlotCount = 0;
	TArray<FCorridorAttackSlot> ActiveCorridorAttackLayout;
	TArray<FCorridorAttackSlot> PendingCorridorAttackLayout;
	float PendingCorridorCapacityElapsed = 0.0f;
	TArray<FVector> CorridorAttackProbeLocations;
	TArray<uint8> CorridorAttackProbeValid;
	TArray<uint8> CorridorAttackProbeSelected;
	TArray<FCorridorRowSlot> CorridorWaitRowLayout;
	TArray<FCorridorRowSlot> CorridorHoldingRowLayout;
	TArray<FCorridorRowSlot> PendingCorridorWaitRowLayout;
	TArray<FCorridorRowSlot> PendingCorridorHoldingRowLayout;
	float CorridorPendingRowStartDistance = 0.0f;
	float PendingCorridorRowStartDistance = 0.0f;
	float PendingCorridorRowLayoutElapsed = 0.0f;
	int32 ActivePocketAttackSlotCount = 1;
	int32 DesiredPocketAttackSlotCount = 1;
	int32 CorridorAxisProbeIndex = INDEX_NONE;
	int32 CorridorWidthProbeIndex = INDEX_NONE;
	TArray<FVector> SpaceProbeEndpoints;
	TArray<uint8> SpaceProbeBlocked;
};
