// Copyright ProjectBH. All Rights Reserved.

#include "BHLargeEnemyEngagementPolicy.h"

#include "GameFramework/Actor.h"

bool FBHLargeEnemyEngagementPolicy::TryGetActiveWedgeGeometry(
	const FVector& OwnerLocation,
	const FVector& LargeEnemyLocation,
	const FBHLargeEnemyWedgeSettings& Settings,
	FVector& OutDirection,
	float& OutRadius)
{
	OutDirection = FVector::ZeroVector;
	OutRadius = 0.0f;
	FVector OwnerToLarge = LargeEnemyLocation - OwnerLocation;
	if (FMath::Abs(OwnerToLarge.Z) > FMath::Max(0.0f, Settings.HeightTolerance))
	{
		return false;
	}

	OwnerToLarge.Z = 0.0f;
	const float ActivationDistance = FMath::Max(0.0f, Settings.ActivationDistance);
	if (OwnerToLarge.IsNearlyZero()
		|| OwnerToLarge.SizeSquared2D() > FMath::Square(ActivationDistance))
	{
		return false;
	}

	const float LargeEnemyDistance = OwnerToLarge.Size2D();
	OutDirection = OwnerToLarge.GetSafeNormal2D();
	OutRadius = FMath::Min(
		FMath::Max(0.0f, Settings.Radius),
		LargeEnemyDistance + FMath::Max(0.0f, Settings.RearDepth));
	return !OutDirection.IsNearlyZero() && OutRadius > UE_SMALL_NUMBER;
}

bool FBHLargeEnemyEngagementPolicy::IsLocationInsideAnyWedge(
	const FVector& OwnerLocation,
	const FVector& SlotLocation,
	const FBHLargeEnemyWedgeSettings& Settings,
	TConstArrayView<FVector> LargeEnemyLocations)
{
	FVector OwnerToSlot = SlotLocation - OwnerLocation;
	if (FMath::Abs(OwnerToSlot.Z) > FMath::Max(0.0f, Settings.HeightTolerance))
	{
		return false;
	}

	OwnerToSlot.Z = 0.0f;
	const float SlotRadius = OwnerToSlot.Size2D();
	if (SlotRadius <= UE_SMALL_NUMBER || SlotRadius > FMath::Max(0.0f, Settings.Radius))
	{
		return false;
	}

	const FVector SlotDirection = OwnerToSlot / SlotRadius;
	const float MinimumDirectionDot = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(Settings.HalfAngleDegrees, 0.0f, 90.0f)));
	for (const FVector& LargeEnemyLocation : LargeEnemyLocations)
	{
		FVector WedgeDirection;
		float WedgeRadius = 0.0f;
		if (TryGetActiveWedgeGeometry(
			OwnerLocation,
			LargeEnemyLocation,
			Settings,
			WedgeDirection,
			WedgeRadius)
			&& SlotRadius <= WedgeRadius
			&& FVector::DotProduct(SlotDirection, WedgeDirection) >= MinimumDirectionDot)
		{
			return true;
		}
	}

	return false;
}

FBHLargeEnemyAttackPolicyPlan FBHLargeEnemyEngagementPolicy::BuildAttackReservationPlan(
	const FBHLargeEnemyAttackPolicyInput& Input)
{
	FBHLargeEnemyAttackPolicyPlan BestPlan;
	int32 BestVictimCount = TNumericLimits<int32>::Max();
	float BestPathScore = TNumericLimits<float>::Max();
	bool bBestUsesPreferredSide = false;

	for (const FBHAttackSlotPolicyCandidate& Candidate : Input.Candidates)
	{
		if (Candidate.SlotIndex == INDEX_NONE || !FMath::IsFinite(Candidate.PathScore))
		{
			continue;
		}

		bool bBlockedByLarge = false;
		int32 RemainingAttackCost = 0;
		TArray<AActor*> CandidateVictims;
		TArray<const FBHAttackOwnerPolicyView*> RemainingNormalOwners;
		for (const FBHAttackOwnerPolicyView& Owner : Input.Owners)
		{
			if (!Owner.Requester)
			{
				continue;
			}

			const float RequiredClearance = FMath::Max(
				FMath::Max(0.0f, Input.RequesterExclusionRadius),
				FMath::Max(0.0f, Owner.ExclusionRadius));
			const bool bConflicts = Owner.SlotIndex == Candidate.SlotIndex
				|| !Owner.bHasValidSlotLocation
				|| (RequiredClearance > 0.0f
					&& FVector::DistSquared2D(Candidate.SlotLocation, Owner.SlotLocation)
						< FMath::Square(RequiredClearance));

			if (Owner.bLarge)
			{
				if (bConflicts)
				{
					bBlockedByLarge = true;
					break;
				}
				RemainingAttackCost += FMath::Max(1, Owner.Cost);
				continue;
			}

			if (bConflicts)
			{
				CandidateVictims.AddUnique(Owner.Requester);
				continue;
			}

			RemainingNormalOwners.Add(&Owner);
			RemainingAttackCost += FMath::Max(1, Owner.Cost);
		}
		if (bBlockedByLarge)
		{
			continue;
		}

		RemainingNormalOwners.Sort(
			[&Candidate](const FBHAttackOwnerPolicyView& First, const FBHAttackOwnerPolicyView& Second)
			{
				const float FirstDistance = FVector::DistSquared2D(
					First.SlotLocation,
					Candidate.SlotLocation);
				const float SecondDistance = FVector::DistSquared2D(
					Second.SlotLocation,
					Candidate.SlotLocation);
				return !FMath::IsNearlyEqual(FirstDistance, SecondDistance)
					? FirstDistance < SecondDistance
					: First.SlotIndex < Second.SlotIndex;
			});
		for (const FBHAttackOwnerPolicyView* RemainingOwner : RemainingNormalOwners)
		{
			if (RemainingAttackCost + FMath::Max(1, Input.RequesterCost)
				<= FMath::Max(0, Input.ActiveAttackCapacity))
			{
				break;
			}
			CandidateVictims.AddUnique(RemainingOwner->Requester);
			RemainingAttackCost -= FMath::Max(1, RemainingOwner->Cost);
		}

		const bool bExceedsCapacity = RemainingAttackCost
			+ FMath::Max(1, Input.RequesterCost)
			> FMath::Max(0, Input.ActiveAttackCapacity);
		const bool bMayUseOversizedReservation = RemainingAttackCost == 0
			&& Input.bAllowRequesterOverCapacityWhenAlone;
		if (bExceedsCapacity && !bMayUseOversizedReservation)
		{
			continue;
		}

		const bool bFewerVictims = CandidateVictims.Num() < BestVictimCount;
		const bool bSameVictimsPreferredSide = CandidateVictims.Num() == BestVictimCount
			&& Candidate.bPreferredCorridorSide && !bBestUsesPreferredSide;
		const bool bSamePriorityBetterPath = CandidateVictims.Num() == BestVictimCount
			&& Candidate.bPreferredCorridorSide == bBestUsesPreferredSide
			&& (Candidate.PathScore < BestPathScore
				|| (FMath::IsNearlyEqual(Candidate.PathScore, BestPathScore)
					&& Candidate.SlotIndex < BestPlan.AttackSlotIndex));
		if (!BestPlan.IsValid()
			|| bFewerVictims
			|| bSameVictimsPreferredSide
			|| bSamePriorityBetterPath)
		{
			BestPlan.AttackSlotIndex = Candidate.SlotIndex;
			BestPlan.NormalOwnersToYield = MoveTemp(CandidateVictims);
			BestVictimCount = BestPlan.NormalOwnersToYield.Num();
			BestPathScore = Candidate.PathScore;
			bBestUsesPreferredSide = Candidate.bPreferredCorridorSide;
		}
	}

	return BestPlan;
}
