// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;

/** Tunable geometry for the player-centered space reserved by a Large enemy. */
struct FBHLargeEnemyWedgeSettings
{
	float ActivationDistance = 0.0f;
	float Radius = 0.0f;
	float RearDepth = 0.0f;
	float HalfAngleDegrees = 0.0f;
	float HeightTolerance = 0.0f;
};

/** Read-only attack owner state consumed by the Large-enemy priority policy. */
struct FBHAttackOwnerPolicyView
{
	AActor* Requester = nullptr;
	int32 SlotIndex = INDEX_NONE;
	FVector SlotLocation = FVector::ZeroVector;
	int32 Cost = 1;
	float ExclusionRadius = 0.0f;
	bool bLarge = false;
	bool bHasValidSlotLocation = false;
};

/** A reachable attack position that may be selected by a Large enemy. */
struct FBHAttackSlotPolicyCandidate
{
	int32 SlotIndex = INDEX_NONE;
	FVector SlotLocation = FVector::ZeroVector;
	float PathScore = TNumericLimits<float>::Max();
	bool bPreferredCorridorSide = false;
};

/** Immutable input used to compute a reservation change without mutating the manager. */
struct FBHLargeEnemyAttackPolicyInput
{
	int32 ActiveAttackCapacity = 0;
	int32 RequesterCost = 1;
	float RequesterExclusionRadius = 0.0f;
	/** Large enemies are guaranteed one Attack reservation even when their cost alone exceeds capacity. */
	bool bAllowRequesterOverCapacityWhenAlone = true;
	TArray<FBHAttackOwnerPolicyView> Owners;
	TArray<FBHAttackSlotPolicyCandidate> Candidates;
};

/** Optional observation of values already computed by the policy. */
struct FBHLargeReservationEvaluation
{
	int32 SlotIndex = INDEX_NONE;
	bool bValid = false;
	const TCHAR* RejectedReason = TEXT("");
	int32 YieldCount = INDEX_NONE;
};

/** Complete change plan applied as one reservation update by UCombatEngagementSlotComponent. */
struct FBHLargeEnemyAttackPolicyPlan
{
	int32 AttackSlotIndex = INDEX_NONE;
	TArray<AActor*> NormalOwnersToYield;

	bool IsValid() const { return AttackSlotIndex != INDEX_NONE; }
};

/**
 * Stateless rules for Large-enemy formation ownership.
 *
 * This class knows no slot-manager state and performs no Actor mutation. It can
 * therefore be reasoned about and tested independently from navigation and AI.
 */
class PROJECTBH_API FBHLargeEnemyEngagementPolicy final
{
public:
	/** Resolves the active direction and its player-centered radial reach. */
	static bool TryGetActiveWedgeGeometry(
		const FVector& OwnerLocation,
		const FVector& LargeEnemyLocation,
		const FBHLargeEnemyWedgeSettings& Settings,
		FVector& OutDirection,
		float& OutRadius);

	/** True when a slot lies inside any active Large-enemy wedge. */
	static bool IsLocationInsideAnyWedge(
		const FVector& OwnerLocation,
		const FVector& SlotLocation,
		const FBHLargeEnemyWedgeSettings& Settings,
		TConstArrayView<FVector> LargeEnemyLocations);

	/** Selects one Attack slot and the minimum Normal set that must yield. */
	static FBHLargeEnemyAttackPolicyPlan BuildAttackReservationPlan(
		const FBHLargeEnemyAttackPolicyInput& Input,
		TArray<FBHLargeReservationEvaluation>* Observations = nullptr);
};
