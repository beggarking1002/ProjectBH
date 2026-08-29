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

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "1", UIMin = "1"))
	int32 AttackSlotCount = 4;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "1", UIMin = "1"))
	int32 WaitSlotCount = 8;

	/** Outer positions for alive enemies that cannot enter the 12-slot combat formation yet. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "1", UIMin = "1"))
	int32 HoldingSlotCount = 16;

	/** Kept inside the current 150-unit basic attack start range. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float AttackRingRadius = 125.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float WaitRingRadius = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float HoldingRingRadius = 500.0f;

	/** First overflow radius used by queued enemies beyond Holding capacity. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Pending Queue", meta = (ClampMin = "0.0", Units = "cm"))
	float PendingRingRadius = 700.0f;

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

	TArray<TWeakObjectPtr<AActor>> AttackReservations;
	TArray<TWeakObjectPtr<AActor>> WaitReservations;
	TArray<TWeakObjectPtr<AActor>> HoldingReservations;

	struct FEngagementQueueEntry
	{
		TWeakObjectPtr<AActor> Requester;
		uint64 Sequence = 0;
		float AttackEligibleTime = 0.0f;
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
};
