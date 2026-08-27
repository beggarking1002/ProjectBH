// Copyright ProjectBH. All Rights Reserved.

#include "CombatEngagementSlotComponent.h"

#include "../../AI/BHCrowdEnemyAIController.h"
#include "../../BHHeroCharacter.h"
#include "../../Enemies/BHEnemy.h"
#include "../../ProjectBH.h"
#include "AI/NavigationSystemBase.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"

UCombatEngagementSlotComponent::UCombatEngagementSlotComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UCombatEngagementSlotComponent::BeginPlay()
{
	Super::BeginPlay();

	InitializeSlots();
	LastReformOwnerLocation = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

void UCombatEngagementSlotComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AttackReservations.Reset();
	WaitReservations.Reset();

	Super::EndPlay(EndPlayReason);
}

void UCombatEngagementSlotComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	PruneInvalidReservations();
	TryReformFormation();
	if (!bDrawDebugSlots)
	{
		return;
	}

	UpdateDebugMetrics();
	DrawDebugSlots();
}

bool UCombatEngagementSlotComponent::TryReserveAttackSlot(
	AActor* Requester,
	float MaxDistanceFromOwner,
	int32 ExcludedSlotIndex)
{
	if (!Requester || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	PruneInvalidReservations();

	int32 ExistingAttackIndex = INDEX_NONE;
	if (FindReservation(AttackReservations, Requester, ExistingAttackIndex))
	{
		FVector ExistingSlotLocation;
		const bool bHasValidLocation = GetSlotWorldLocation(
			EBHCombatSlotType::Attack,
			ExistingAttackIndex,
			ExistingSlotLocation);
		const bool bWithinAttackRange = bHasValidLocation
			&& FVector::DistSquared2D(GetOwner()->GetActorLocation(), ExistingSlotLocation)
				<= FMath::Square(MaxDistanceFromOwner);
		if (bWithinAttackRange)
		{
			return true;
		}

		AttackReservations[ExistingAttackIndex].Reset();
	}

	if (!TryReserveSlot(Requester, EBHCombatSlotType::Attack, MaxDistanceFromOwner, ExcludedSlotIndex))
	{
		return false;
	}

	for (TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
		}
	}

	return true;
}

bool UCombatEngagementSlotComponent::TryReserveWaitSlot(AActor* Requester, int32 ExcludedSlotIndex)
{
	if (!Requester || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	PruneInvalidReservations();

	int32 ExistingAttackIndex = INDEX_NONE;
	if (FindReservation(AttackReservations, Requester, ExistingAttackIndex))
	{
		return false;
	}

	int32 ExistingWaitIndex = INDEX_NONE;
	if (FindReservation(WaitReservations, Requester, ExistingWaitIndex))
	{
		return true;
	}

	return TryReserveSlot(Requester, EBHCombatSlotType::Wait, -1.0f, ExcludedSlotIndex);
}

bool UCombatEngagementSlotComponent::GetReservedSlot(
	AActor* Requester,
	EBHCombatSlotType& OutSlotType,
	int32& OutSlotIndex,
	FVector& OutWorldLocation) const
{
	OutSlotType = EBHCombatSlotType::None;
	OutSlotIndex = INDEX_NONE;
	OutWorldLocation = FVector::ZeroVector;

	if (!Requester)
	{
		return false;
	}

	if (FindReservation(AttackReservations, Requester, OutSlotIndex))
	{
		OutSlotType = EBHCombatSlotType::Attack;
		return GetSlotWorldLocation(OutSlotType, OutSlotIndex, OutWorldLocation);
	}

	if (FindReservation(WaitReservations, Requester, OutSlotIndex))
	{
		OutSlotType = EBHCombatSlotType::Wait;
		return GetSlotWorldLocation(OutSlotType, OutSlotIndex, OutWorldLocation);
	}

	return false;
}

bool UCombatEngagementSlotComponent::GetMoveGoalForReservedSlot(
	AActor* Requester,
	EBHCombatSlotType SlotType,
	int32 SlotIndex,
	const FVector& FinalSlotLocation,
	FVector& OutMoveGoal,
	bool& bOutUsesStagedRoute) const
{
	OutMoveGoal = FinalSlotLocation;
	bOutUsesStagedRoute = false;

	const AActor* Owner = GetOwner();
	if (!Requester || !Owner || SlotType == EBHCombatSlotType::None || SlotIndex == INDEX_NONE)
	{
		return false;
	}

	const FVector RequesterLocation = Requester->GetActorLocation();
	if (!DoesSegmentCrossCombatCore(RequesterLocation, FinalSlotLocation))
	{
		return true;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	FVector RequesterDirection = RequesterLocation - OwnerLocation;
	RequesterDirection.Z = 0.0f;
	FVector TargetDirection = FinalSlotLocation - OwnerLocation;
	TargetDirection.Z = 0.0f;
	if (TargetDirection.IsNearlyZero())
	{
		return false;
	}

	TargetDirection.Normalize();
	if (RequesterDirection.IsNearlyZero())
	{
		RequesterDirection = -TargetDirection;
	}
	const float RequesterRadius = RequesterDirection.Size2D();
	RequesterDirection.Normalize();

	const float OrbitRadius = FMath::Max3(
		WaitRingRadius,
		AttackRingRadius + OrbitRingAcceptanceRadius,
		GetEffectiveCombatCoreRadius() + OrbitRingAcceptanceRadius);
	FVector DesiredWaypoint;
	if (FMath::Abs(RequesterRadius - OrbitRadius) > OrbitRingAcceptanceRadius)
	{
		DesiredWaypoint = OwnerLocation + RequesterDirection * OrbitRadius;
	}
	else
	{
		const float RequesterAngle = FMath::RadiansToDegrees(FMath::Atan2(RequesterDirection.Y, RequesterDirection.X));
		const float TargetAngle = FMath::RadiansToDegrees(FMath::Atan2(TargetDirection.Y, TargetDirection.X));
		const float DeltaAngle = FMath::FindDeltaAngleDegrees(RequesterAngle, TargetAngle);
		const float StepAngle = FMath::Clamp(
			DeltaAngle,
			-OrbitWaypointAngleStep,
			OrbitWaypointAngleStep);
		const float NextAngleRadians = FMath::DegreesToRadians(RequesterAngle + StepAngle);
		DesiredWaypoint = OwnerLocation
			+ FVector(FMath::Cos(NextAngleRadians), FMath::Sin(NextAngleRadians), 0.0f) * OrbitRadius;
	}

	if (!ProjectToNavigation(DesiredWaypoint, OutMoveGoal))
	{
		return false;
	}

	bOutUsesStagedRoute = true;
	return true;
}

void UCombatEngagementSlotComponent::ReleaseSlot(AActor* Requester)
{
	if (!Requester || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	for (TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
		}
	}

	for (TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
		}
	}
}

void UCombatEngagementSlotComponent::InitializeSlots()
{
	AttackReservations.SetNum(FMath::Max(1, AttackSlotCount));
	WaitReservations.SetNum(FMath::Max(1, WaitSlotCount));
}

void UCombatEngagementSlotComponent::PruneInvalidReservations()
{
	for (TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		if (!Reservation.IsValid())
		{
			Reservation.Reset();
		}
	}

	for (TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		if (!Reservation.IsValid())
		{
			Reservation.Reset();
		}
	}
}

void UCombatEngagementSlotComponent::TryReformFormation()
{
	const AActor* Owner = GetOwner();
	if (!Owner || ReformTriggerDistance <= 0.0f)
	{
		return;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	if (FVector::DistSquared2D(LastReformOwnerLocation, OwnerLocation) < FMath::Square(ReformTriggerDistance))
	{
		return;
	}

	LastReformOwnerLocation = OwnerLocation;
	ReformReservations();
}

void UCombatEngagementSlotComponent::ReformReservations()
{
	ReformRingReservations(AttackReservations, EBHCombatSlotType::Attack);
	ReformRingReservations(WaitReservations, EBHCombatSlotType::Wait);
	++FormationRevision;

	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s reformed engagement rings. Revision: %d"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		FormationRevision);
}

void UCombatEngagementSlotComponent::ReformRingReservations(
	TArray<TWeakObjectPtr<AActor>>& Reservations,
	EBHCombatSlotType SlotType)
{
	TArray<FVector, TInlineAllocator<16>> SlotLocations;
	SlotLocations.Reserve(Reservations.Num());
	for (int32 SlotIndex = 0; SlotIndex < Reservations.Num(); ++SlotIndex)
	{
		FVector SlotLocation;
		if (!GetSlotWorldLocation(SlotType, SlotIndex, SlotLocation))
		{
			return;
		}
		SlotLocations.Add(SlotLocation);
	}

	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<16>> Requesters;
	TArray<int32, TInlineAllocator<16>> AvailableSlotIndices;
	for (int32 SlotIndex = 0; SlotIndex < Reservations.Num(); ++SlotIndex)
	{
		if (Reservations[SlotIndex].IsValid())
		{
			Requesters.Add(Reservations[SlotIndex]);
		}
		Reservations[SlotIndex].Reset();
		AvailableSlotIndices.Add(SlotIndex);
	}

	// Preserve Attack/Wait roles, then minimize conspicuous cross-ring travel by
	// repeatedly selecting the closest remaining actor-slot pair.
	while (!Requesters.IsEmpty() && !AvailableSlotIndices.IsEmpty())
	{
		int32 BestRequesterArrayIndex = INDEX_NONE;
		int32 BestSlotArrayIndex = INDEX_NONE;
		float BestDistanceSquared = TNumericLimits<float>::Max();

		for (int32 RequesterArrayIndex = 0; RequesterArrayIndex < Requesters.Num(); ++RequesterArrayIndex)
		{
			const AActor* Requester = Requesters[RequesterArrayIndex].Get();
			if (!Requester)
			{
				continue;
			}

			for (int32 SlotArrayIndex = 0; SlotArrayIndex < AvailableSlotIndices.Num(); ++SlotArrayIndex)
			{
				const FVector& SlotLocation = SlotLocations[AvailableSlotIndices[SlotArrayIndex]];
				const float DistanceSquared = FVector::DistSquared2D(Requester->GetActorLocation(), SlotLocation);
				if (DistanceSquared < BestDistanceSquared)
				{
					BestDistanceSquared = DistanceSquared;
					BestRequesterArrayIndex = RequesterArrayIndex;
					BestSlotArrayIndex = SlotArrayIndex;
				}
			}
		}

		if (BestRequesterArrayIndex == INDEX_NONE || BestSlotArrayIndex == INDEX_NONE)
		{
			break;
		}

		const int32 ReservedSlotIndex = AvailableSlotIndices[BestSlotArrayIndex];
		Reservations[ReservedSlotIndex] = Requesters[BestRequesterArrayIndex];
		Requesters.RemoveAtSwap(BestRequesterArrayIndex, 1, EAllowShrinking::No);
		AvailableSlotIndices.RemoveAtSwap(BestSlotArrayIndex, 1, EAllowShrinking::No);
	}
}

bool UCombatEngagementSlotComponent::TryReserveSlot(
	AActor* Requester,
	EBHCombatSlotType SlotType,
	float MaxDistanceFromOwner,
	int32 ExcludedSlotIndex)
{
	TArray<TWeakObjectPtr<AActor>>* Reservations = nullptr;
	switch (SlotType)
	{
	case EBHCombatSlotType::Attack:
		Reservations = &AttackReservations;
		break;
	case EBHCombatSlotType::Wait:
		Reservations = &WaitReservations;
		break;
	default:
		return false;
	}

	int32 BestSlotIndex = INDEX_NONE;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	bool bBestSlotNeedsStagedRoute = true;
	for (int32 SlotIndex = 0; SlotIndex < Reservations->Num(); ++SlotIndex)
	{
		if (SlotIndex == ExcludedSlotIndex)
		{
			continue;
		}

		if ((*Reservations)[SlotIndex].IsValid())
		{
			continue;
		}

		FVector SlotLocation;
		if (!GetSlotWorldLocation(SlotType, SlotIndex, SlotLocation))
		{
			continue;
		}

		if (MaxDistanceFromOwner >= 0.0f
			&& FVector::DistSquared2D(GetOwner()->GetActorLocation(), SlotLocation)
				> FMath::Square(MaxDistanceFromOwner))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared2D(Requester->GetActorLocation(), SlotLocation);
		const bool bNeedsStagedRoute = DoesSegmentCrossCombatCore(Requester->GetActorLocation(), SlotLocation);
		const bool bPreferDirectSlot = bBestSlotNeedsStagedRoute && !bNeedsStagedRoute;
		if (BestSlotIndex == INDEX_NONE
			|| bPreferDirectSlot
			|| (bBestSlotNeedsStagedRoute == bNeedsStagedRoute && DistanceSquared < BestDistanceSquared))
		{
			BestDistanceSquared = DistanceSquared;
			BestSlotIndex = SlotIndex;
			bBestSlotNeedsStagedRoute = bNeedsStagedRoute;
		}
	}

	if (BestSlotIndex == INDEX_NONE)
	{
		return false;
	}

	(*Reservations)[BestSlotIndex] = Requester;
	return true;
}

bool UCombatEngagementSlotComponent::FindReservation(
	const TArray<TWeakObjectPtr<AActor>>& Reservations,
	AActor* Requester,
	int32& OutSlotIndex) const
{
	OutSlotIndex = INDEX_NONE;
	for (int32 SlotIndex = 0; SlotIndex < Reservations.Num(); ++SlotIndex)
	{
		if (Reservations[SlotIndex].Get() == Requester)
		{
			OutSlotIndex = SlotIndex;
			return true;
		}
	}

	return false;
}

bool UCombatEngagementSlotComponent::GetSlotWorldLocation(
	EBHCombatSlotType SlotType,
	int32 SlotIndex,
	FVector& OutWorldLocation) const
{
	const AActor* Owner = GetOwner();
	const int32 SlotCount = SlotType == EBHCombatSlotType::Attack ? AttackReservations.Num() : WaitReservations.Num();
	if (!Owner || SlotType == EBHCombatSlotType::None || !FMath::IsWithin(SlotIndex, 0, SlotCount))
	{
		return false;
	}

	const float RingRadius = SlotType == EBHCombatSlotType::Attack ? AttackRingRadius : WaitRingRadius;
	const float AngleOffset = SlotType == EBHCombatSlotType::Attack ? AttackRingAngleOffset : WaitRingAngleOffset;
	const float AngleDegrees = AngleOffset + (360.0f * static_cast<float>(SlotIndex) / static_cast<float>(SlotCount));
	const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
	const FVector RingDirection(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0f);
	const FVector DesiredLocation = Owner->GetActorLocation() + RingDirection * RingRadius;

	return ProjectToNavigation(DesiredLocation, OutWorldLocation);
}

bool UCombatEngagementSlotComponent::DoesSegmentCrossCombatCore(
	const FVector& SegmentStart,
	const FVector& SegmentEnd) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	const FVector2D Center(Owner->GetActorLocation());
	const FVector2D Start(SegmentStart);
	const FVector2D End(SegmentEnd);
	const FVector2D Segment = End - Start;
	const float SegmentLengthSquared = Segment.SizeSquared();
	const float EndRadius = FVector2D::Distance(Center, End);
	const float EffectiveRadius = FMath::Min(
		GetEffectiveCombatCoreRadius(),
		FMath::Max(0.0f, EndRadius - 5.0f));
	if (EffectiveRadius <= 0.0f)
	{
		return false;
	}

	if (SegmentLengthSquared <= UE_KINDA_SMALL_NUMBER)
	{
		return FVector2D::DistSquared(Start, Center) < FMath::Square(EffectiveRadius);
	}

	const float ClosestAlpha = FMath::Clamp(
		FVector2D::DotProduct(Center - Start, Segment) / SegmentLengthSquared,
		0.0f,
		1.0f);
	const FVector2D ClosestPoint = Start + Segment * ClosestAlpha;
	return FVector2D::DistSquared(ClosestPoint, Center) < FMath::Square(EffectiveRadius);
}

float UCombatEngagementSlotComponent::GetEffectiveCombatCoreRadius() const
{
	return FMath::Min(
		FMath::Max(0.0f, CombatCoreRadius),
		FMath::Max(0.0f, AttackRingRadius - 5.0f));
}

bool UCombatEngagementSlotComponent::ProjectToNavigation(const FVector& DesiredLocation, FVector& OutProjectedLocation) const
{
	const UWorld* World = GetWorld();
	const UNavigationSystemV1* NavigationSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavigationSystem)
	{
		return false;
	}

	FNavLocation ProjectedLocation;
	if (!NavigationSystem->ProjectPointToNavigation(DesiredLocation, ProjectedLocation, NavProjectionExtent))
	{
		return false;
	}

	OutProjectedLocation = ProjectedLocation.Location;
	return true;
}

void UCombatEngagementSlotComponent::UpdateDebugMetrics()
{
	CurrentSpacingViolationCount = 0;
	CurrentAttackingEnemyCount = 0;
	CurrentWaitAttackerCount = 0;

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	TArray<ABHEnemy*, TInlineAllocator<16>> EngagedEnemies;
	for (TActorIterator<ABHEnemy> It(World); It; ++It)
	{
		ABHEnemy* Enemy = *It;
		const ABHCrowdEnemyAIController* Controller = Enemy
			? Cast<ABHCrowdEnemyAIController>(Enemy->GetController())
			: nullptr;
		if (!Controller || Controller->GetCurrentTarget() != Owner)
		{
			continue;
		}

		EngagedEnemies.Add(Enemy);
		if (Enemy->GetCombatState() == EBHEnemyCombatState::Attacking)
		{
			++CurrentAttackingEnemyCount;
			int32 WaitSlotIndex = INDEX_NONE;
			if (FindReservation(WaitReservations, Enemy, WaitSlotIndex))
			{
				++CurrentWaitAttackerCount;
			}
		}
	}

	for (int32 FirstIndex = 0; FirstIndex < EngagedEnemies.Num(); ++FirstIndex)
	{
		const ABHEnemy* FirstEnemy = EngagedEnemies[FirstIndex];
		const UCapsuleComponent* FirstCapsule = FirstEnemy ? FirstEnemy->GetCapsuleComponent() : nullptr;
		if (!FirstCapsule)
		{
			continue;
		}

		for (int32 SecondIndex = FirstIndex + 1; SecondIndex < EngagedEnemies.Num(); ++SecondIndex)
		{
			const ABHEnemy* SecondEnemy = EngagedEnemies[SecondIndex];
			const UCapsuleComponent* SecondCapsule = SecondEnemy ? SecondEnemy->GetCapsuleComponent() : nullptr;
			if (!SecondCapsule)
			{
				continue;
			}

			const FVector FirstLocation = FirstEnemy->GetActorLocation();
			const FVector SecondLocation = SecondEnemy->GetActorLocation();
			const float SameLayerTolerance = FMath::Max(
				FirstCapsule->GetScaledCapsuleHalfHeight(),
				SecondCapsule->GetScaledCapsuleHalfHeight());
			if (FMath::Abs(FirstLocation.Z - SecondLocation.Z) > SameLayerTolerance)
			{
				continue;
			}

			const float MinimumSpacing = FirstCapsule->GetScaledCapsuleRadius()
				+ SecondCapsule->GetScaledCapsuleRadius()
				- 5.0f;
			if (FVector::DistSquared2D(FirstLocation, SecondLocation) < FMath::Square(MinimumSpacing))
			{
				++CurrentSpacingViolationCount;
				DrawDebugLine(World, FirstLocation, SecondLocation, FColor::Magenta, false, 0.12f, 0, 3.0f);
			}
		}
	}

	PeakSpacingViolationCount = FMath::Max(PeakSpacingViolationCount, CurrentSpacingViolationCount);
}

void UCombatEngagementSlotComponent::DrawDebugSlots() const
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	DrawDebugCircle(
		World,
		Owner->GetActorLocation(),
		GetEffectiveCombatCoreRadius(),
		48,
		FColor::Orange,
		false,
		0.12f,
		0,
		2.0f,
		FVector::ForwardVector,
		FVector::RightVector,
		false);

	for (int32 SlotIndex = 0; SlotIndex < AttackReservations.Num(); ++SlotIndex)
	{
		FVector SlotLocation;
		if (GetSlotWorldLocation(EBHCombatSlotType::Attack, SlotIndex, SlotLocation))
		{
			const FColor Color = AttackReservations[SlotIndex].IsValid() ? FColor::Red : FColor::Green;
			DrawDebugSphere(World, SlotLocation, 22.0f, 12, Color, false, 0.12f, 0, 2.0f);
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < WaitReservations.Num(); ++SlotIndex)
	{
		FVector SlotLocation;
		if (GetSlotWorldLocation(EBHCombatSlotType::Wait, SlotIndex, SlotLocation))
		{
			const FColor Color = WaitReservations[SlotIndex].IsValid() ? FColor::Yellow : FColor::Cyan;
			DrawDebugSphere(World, SlotLocation, 16.0f, 10, Color, false, 0.12f, 0, 1.5f);
		}
	}

	int32 OccupiedAttackSlots = 0;
	for (const TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		OccupiedAttackSlots += Reservation.IsValid() ? 1 : 0;
	}

	int32 OccupiedWaitSlots = 0;
	for (const TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		OccupiedWaitSlots += Reservation.IsValid() ? 1 : 0;
	}

	const FString DebugText = FString::Printf(
		TEXT("Slots A:%d/%d W:%d/%d | Reform:%d | Attacking:%d WaitAttack:%d | Spacing:%d Peak:%d"),
		OccupiedAttackSlots,
		AttackReservations.Num(),
		OccupiedWaitSlots,
		WaitReservations.Num(),
		FormationRevision,
		CurrentAttackingEnemyCount,
		CurrentWaitAttackerCount,
		CurrentSpacingViolationCount,
		PeakSpacingViolationCount);
	DrawDebugString(
		World,
		Owner->GetActorLocation() + FVector(0.0f, 0.0f, 170.0f),
		DebugText,
		nullptr,
		CurrentWaitAttackerCount > 0 ? FColor::Red : FColor::White,
		0.12f,
		false,
		1.1f);
}
