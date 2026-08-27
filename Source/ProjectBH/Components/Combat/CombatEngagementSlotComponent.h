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
	Wait
};

/**
 * Server-authoritative reservation manager for combat positions around its owner.
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
	bool TryReserveAttackSlot(AActor* Requester, float MaxDistanceFromOwner, int32 ExcludedSlotIndex = INDEX_NONE);

	/** Keeps an existing Wait reservation or reserves one if the requester has no Attack slot. */
	bool TryReserveWaitSlot(AActor* Requester, int32 ExcludedSlotIndex = INDEX_NONE);

	/** Returns the requester's current slot and its latest NavMesh-projected world position. */
	bool GetReservedSlot(AActor* Requester, EBHCombatSlotType& OutSlotType, int32& OutSlotIndex, FVector& OutWorldLocation) const;

	/** Resolves either the final slot or the next Wait Ring waypoint without crossing the player's Combat Core. */
	bool GetMoveGoalForReservedSlot(
		AActor* Requester,
		EBHCombatSlotType SlotType,
		int32 SlotIndex,
		const FVector& FinalSlotLocation,
		FVector& OutMoveGoal,
		bool& bOutUsesStagedRoute) const;

	/** Releases every slot owned by Requester. Safe to call repeatedly. */
	void ReleaseSlot(AActor* Requester);

	UFUNCTION(BlueprintPure, Category = "Combat|Engagement Slots")
	int32 GetAttackSlotCount() const { return AttackSlotCount; }

	UFUNCTION(BlueprintPure, Category = "Combat|Engagement Slots")
	int32 GetWaitSlotCount() const { return WaitSlotCount; }

	UFUNCTION(BlueprintPure, Category = "Debug|Engagement Slots")
	int32 GetCurrentSpacingViolationCount() const { return CurrentSpacingViolationCount; }

	UFUNCTION(BlueprintPure, Category = "Debug|Engagement Slots")
	int32 GetPeakSpacingViolationCount() const { return PeakSpacingViolationCount; }

	/** Increments whenever a large owner displacement triggers a coordinated ring reform. */
	UFUNCTION(BlueprintPure, Category = "Combat|Engagement Slots")
	int32 GetFormationRevision() const { return FormationRevision; }

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "1", UIMin = "1"))
	int32 AttackSlotCount = 4;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "1", UIMin = "1"))
	int32 WaitSlotCount = 8;

	/** Kept inside the current 150-unit basic attack start range. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float AttackRingRadius = 125.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float WaitRingRadius = 300.0f;

	/** Enemy routes cannot directly cross this player-centered radius. Kept below the Attack Ring radius at runtime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Approach Routing", meta = (ClampMin = "0.0", Units = "cm"))
	float CombatCoreRadius = 100.0f;

	/** Maximum angular step for one orbit waypoint around the Wait Ring. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Approach Routing", meta = (ClampMin = "5.0", ClampMax = "90.0", Units = "deg"))
	float OrbitWaypointAngleStep = 45.0f;

	/** Radial tolerance before an Enemy is treated as already aligned with the Wait Ring. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Approach Routing", meta = (ClampMin = "1.0", Units = "cm"))
	float OrbitRingAcceptanceRadius = 35.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (Units = "deg"))
	float AttackRingAngleOffset = 45.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (Units = "deg"))
	float WaitRingAngleOffset = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots")
	FVector NavProjectionExtent = FVector(50.0f, 50.0f, 200.0f);

	/** Reassigns occupied slots within each ring after the owner moves this far from the last reform anchor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float ReformTriggerDistance = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug|Engagement Slots")
	bool bDrawDebugSlots = true;

private:
	void InitializeSlots();
	void PruneInvalidReservations();
	void TryReformFormation();
	void ReformReservations();
	void ReformRingReservations(TArray<TWeakObjectPtr<AActor>>& Reservations, EBHCombatSlotType SlotType);
	bool TryReserveSlot(AActor* Requester, EBHCombatSlotType SlotType, float MaxDistanceFromOwner = -1.0f, int32 ExcludedSlotIndex = INDEX_NONE);
	bool FindReservation(const TArray<TWeakObjectPtr<AActor>>& Reservations, AActor* Requester, int32& OutSlotIndex) const;
	bool GetSlotWorldLocation(EBHCombatSlotType SlotType, int32 SlotIndex, FVector& OutWorldLocation) const;
	bool DoesSegmentCrossCombatCore(const FVector& SegmentStart, const FVector& SegmentEnd) const;
	float GetEffectiveCombatCoreRadius() const;
	bool ProjectToNavigation(const FVector& DesiredLocation, FVector& OutProjectedLocation) const;
	void UpdateDebugMetrics();
	void DrawDebugSlots() const;

	TArray<TWeakObjectPtr<AActor>> AttackReservations;
	TArray<TWeakObjectPtr<AActor>> WaitReservations;
	FVector LastReformOwnerLocation = FVector::ZeroVector;
	int32 FormationRevision = 0;
	int32 CurrentSpacingViolationCount = 0;
	int32 PeakSpacingViolationCount = 0;
	int32 CurrentAttackingEnemyCount = 0;
	int32 CurrentWaitAttackerCount = 0;
};
