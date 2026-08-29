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
#include "NavigationPath.h"
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
	EngagementAnchorLocation = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	LastReformOwnerLocation = EngagementAnchorLocation;
}

void UCombatEngagementSlotComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AttackReservations.Reset();
	WaitReservations.Reset();
	HoldingReservations.Reset();
	EngagementQueue.Reset();

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
	UpdateEngagementAnchor(DeltaTime);
	RefreshInitialFormationPhase();
	if (!IsInitialFormationActive())
	{
		RefreshPromotions();
	}
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
	int32 ExcludedSlotIndex)
{
	if (!Requester || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	PruneInvalidReservations();
	const float MaxDistanceFromOwner = GetMaximumAttackSlotDistance(Requester);

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
	for (TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
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

	if (!TryReserveSlot(Requester, EBHCombatSlotType::Wait, -1.0f, ExcludedSlotIndex))
	{
		return false;
	}

	for (TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
		}
	}
	return true;
}

bool UCombatEngagementSlotComponent::RequestEngagementSlot(
	AActor* Requester,
	EBHCombatSlotType ExcludedSlotType,
	int32 ExcludedSlotIndex)
{
	if (!Requester || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	PruneInvalidReservations();
	RegisterQueueRequester(Requester);
	if (IsRequesterReserved(Requester))
	{
		return true;
	}

	const int32 ExcludedAttackIndex = ExcludedSlotType == EBHCombatSlotType::Attack
		? ExcludedSlotIndex
		: INDEX_NONE;
	const int32 ExcludedWaitIndex = ExcludedSlotType == EBHCombatSlotType::Wait
		? ExcludedSlotIndex
		: INDEX_NONE;
	const int32 ExcludedHoldingIndex = ExcludedSlotType == EBHCombatSlotType::Holding
		? ExcludedSlotIndex
		: INDEX_NONE;

	// Initial assignments are provisional. The central manager selects Attack
	// owners by navigation path plus live congestion cost once registration settles.
	if (IsInitialFormationActive())
	{
		if (TryReserveWaitSlot(Requester, ExcludedWaitIndex))
		{
			return true;
		}

		return TryReserveSlot(
			Requester,
			EBHCombatSlotType::Holding,
			-1.0f,
			ExcludedHoldingIndex);
	}

	// Runtime arrivals cannot overtake actors already waiting in an outer layer.
	if (!HasAnyValidReservation(WaitReservations)
		&& !HasAnyValidReservation(HoldingReservations)
		&& TryReserveAttackSlot(Requester, ExcludedAttackIndex))
	{
		return true;
	}

	if (!HasAnyValidReservation(HoldingReservations)
		&& TryReserveWaitSlot(Requester, ExcludedWaitIndex))
	{
		return true;
	}

	return TryReserveSlot(
		Requester,
		EBHCombatSlotType::Holding,
		-1.0f,
		ExcludedHoldingIndex);
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

	if (FindReservation(HoldingReservations, Requester, OutSlotIndex))
	{
		OutSlotType = EBHCombatSlotType::Holding;
		return GetSlotWorldLocation(OutSlotType, OutSlotIndex, OutWorldLocation);
	}

	return false;
}

bool UCombatEngagementSlotComponent::GetPendingWaitLocation(
	AActor* Requester,
	int32& OutPendingIndex,
	FVector& OutWorldLocation) const
{
	OutPendingIndex = INDEX_NONE;
	OutWorldLocation = FVector::ZeroVector;
	const AActor* Owner = GetOwner();
	if (!Requester || !Owner || IsRequesterReserved(Requester)
		|| !FindPendingRequesterIndex(Requester, OutPendingIndex))
	{
		return false;
	}

	const int32 SlotsPerRing = FMath::Max(1, PendingSlotsPerRing);
	const int32 RingIndex = OutPendingIndex / SlotsPerRing;
	const int32 SlotIndexOnRing = OutPendingIndex % SlotsPerRing;
	const float RingRadius = FMath::Max(0.0f, PendingRingRadius)
		+ static_cast<float>(RingIndex) * FMath::Max(0.0f, PendingRingSpacing);
	const float AngleDegrees = PendingRingAngleOffset
		+ 360.0f * static_cast<float>(SlotIndexOnRing) / static_cast<float>(SlotsPerRing);
	const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
	const FVector RingDirection(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0f);
	return ProjectToNavigation(EngagementAnchorLocation + RingDirection * RingRadius, OutWorldLocation);
}

bool UCombatEngagementSlotComponent::GetMoveGoalForReservedSlot(
	AActor* Requester,
	EBHCombatSlotType SlotType,
	int32 SlotIndex,
	const FVector& FinalSlotLocation,
	EBHCombatMoveRouteStage PreviousRouteStage,
	FVector& OutMoveGoal,
	EBHCombatMoveRouteStage& OutRouteStage) const
{
	OutMoveGoal = FinalSlotLocation;
	OutRouteStage = EBHCombatMoveRouteStage::Direct;

	const AActor* Owner = GetOwner();
	if (!Requester || !Owner || SlotType == EBHCombatSlotType::None || SlotIndex == INDEX_NONE)
	{
		return false;
	}

	const FVector RequesterLocation = Requester->GetActorLocation();
	const FVector OwnerLocation = Owner->GetActorLocation();
	const FVector RouteCenter = SlotType == EBHCombatSlotType::Attack
		? OwnerLocation
		: EngagementAnchorLocation;
	FVector RequesterDirection = RequesterLocation - RouteCenter;
	RequesterDirection.Z = 0.0f;
	FVector TargetDirection = FinalSlotLocation - RouteCenter;
	TargetDirection.Z = 0.0f;
	const float FinalSlotRadius = TargetDirection.Size2D();
	if (FinalSlotRadius <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	TargetDirection /= FinalSlotRadius;
	const float RequesterRadius = RequesterDirection.Size2D();
	if (RequesterRadius <= UE_KINDA_SMALL_NUMBER)
	{
		RequesterDirection = -TargetDirection;
	}
	else
	{
		RequesterDirection /= RequesterRadius;
	}
	const float RequesterAngle = FMath::RadiansToDegrees(FMath::Atan2(RequesterDirection.Y, RequesterDirection.X));
	const float TargetAngle = FMath::RadiansToDegrees(FMath::Atan2(TargetDirection.Y, TargetDirection.X));
	const float DeltaAngle = FMath::FindDeltaAngleDegrees(RequesterAngle, TargetAngle);
	const bool bDirectPathCrossesCombatCore = DoesSegmentCrossCombatCore(RequesterLocation, FinalSlotLocation);

	float FinalRingRadius = 0.0f;
	float IngressStagingRadius = 0.0f;
	if (SlotType == EBHCombatSlotType::Attack)
	{
		FinalRingRadius = FinalSlotRadius;
		IngressStagingRadius = WaitRingRadius;
	}
	else if (SlotType == EBHCombatSlotType::Wait)
	{
		FinalRingRadius = FinalSlotRadius;
		IngressStagingRadius = HoldingRingRadius;
	}

	const bool bUsesInnerRingIngress = IngressStagingRadius > FinalRingRadius;
	const bool bOutsideFinalRing = bUsesInnerRingIngress
		&& RequesterRadius > FinalRingRadius + OrbitRingAcceptanceRadius;

	// Once inward ingress begins, never send the actor back to the staging ring
	// for the same reservation. This prevents small owner movements from making
	// the route oscillate between alignment and ingress.
	if (PreviousRouteStage == EBHCombatMoveRouteStage::Ingress
		&& bOutsideFinalRing
		&& !bDirectPathCrossesCombatCore)
	{
		OutRouteStage = EBHCombatMoveRouteStage::Ingress;
		return true;
	}

	if (bOutsideFinalRing)
	{
		if (FMath::Abs(DeltaAngle) <= RingIngressAngleTolerance
			&& !bDirectPathCrossesCombatCore)
		{
			OutRouteStage = EBHCombatMoveRouteStage::Ingress;
			return true;
		}

		FVector DesiredWaypoint;
		const bool bContinueRingAlignment = PreviousRouteStage == EBHCombatMoveRouteStage::AlignOnRing;
		const bool bAtIngressStagingRing = FMath::Abs(RequesterRadius - IngressStagingRadius)
			<= OrbitRingAcceptanceRadius;
		if (!bContinueRingAlignment && !bAtIngressStagingRing)
		{
			DesiredWaypoint = RouteCenter + RequesterDirection * IngressStagingRadius;
			OutRouteStage = EBHCombatMoveRouteStage::ApproachRing;
		}
		else
		{
			const float StepAngle = FMath::Clamp(
				DeltaAngle,
				-OrbitWaypointAngleStep,
				OrbitWaypointAngleStep);
			const float NextAngleRadians = FMath::DegreesToRadians(RequesterAngle + StepAngle);
			DesiredWaypoint = RouteCenter
				+ FVector(FMath::Cos(NextAngleRadians), FMath::Sin(NextAngleRadians), 0.0f)
					* IngressStagingRadius;
			OutRouteStage = EBHCombatMoveRouteStage::AlignOnRing;
		}

		if (!ProjectToNavigation(DesiredWaypoint, OutMoveGoal))
		{
			return false;
		}

		return true;
	}

	if (!bDirectPathCrossesCombatCore)
	{
		return true;
	}

	float RouteRingRadius = WaitRingRadius;
	if (SlotType == EBHCombatSlotType::Holding)
	{
		RouteRingRadius = HoldingRingRadius;
	}
	else if (SlotType == EBHCombatSlotType::Pending)
	{
		RouteRingRadius = FinalSlotRadius;
	}
	const float OrbitRadius = FMath::Max3(
		RouteRingRadius,
		AttackRingRadius + OrbitRingAcceptanceRadius,
		GetEffectiveCombatCoreRadius() + OrbitRingAcceptanceRadius);
	FVector DesiredWaypoint;
	if (FMath::Abs(RequesterRadius - OrbitRadius) > OrbitRingAcceptanceRadius)
	{
		DesiredWaypoint = RouteCenter + RequesterDirection * OrbitRadius;
	}
	else
	{
		const float StepAngle = FMath::Clamp(
			DeltaAngle,
			-OrbitWaypointAngleStep,
			OrbitWaypointAngleStep);
		const float NextAngleRadians = FMath::DegreesToRadians(RequesterAngle + StepAngle);
		DesiredWaypoint = RouteCenter
			+ FVector(FMath::Cos(NextAngleRadians), FMath::Sin(NextAngleRadians), 0.0f) * OrbitRadius;
	}

	if (!ProjectToNavigation(DesiredWaypoint, OutMoveGoal))
	{
		return false;
	}

	OutRouteStage = FMath::Abs(RequesterRadius - OrbitRadius) > OrbitRingAcceptanceRadius
		? EBHCombatMoveRouteStage::ApproachRing
		: EBHCombatMoveRouteStage::AlignOnRing;
	return true;
}

void UCombatEngagementSlotComponent::ReleaseSlot(AActor* Requester, bool bPreserveQueuePosition)
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

	for (TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
		}
	}

	if (!bPreserveQueuePosition)
	{
		RemoveQueueRequester(Requester);
	}
	if (EngagementQueue.IsEmpty())
	{
		bInitialFormationActive = true;
		LastRequesterRegistrationTime = 0.0f;
	}
}

bool UCombatEngagementSlotComponent::HandleStalledAttackReservation(
	AActor* Requester,
	float AttackReentryCooldown)
{
	if (!Requester || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	PruneInvalidReservations();
	int32 AttackSlotIndex = INDEX_NONE;
	if (!FindReservation(AttackReservations, Requester, AttackSlotIndex))
	{
		return false;
	}

	int32 WaitSlotIndex = INDEX_NONE;
	if (!FindBestWaitAdmissionForAttackSlot(AttackSlotIndex, WaitSlotIndex))
	{
		return false;
	}

	AActor* PromotedRequester = WaitReservations[WaitSlotIndex].Get();
	if (!PromotedRequester)
	{
		return false;
	}

	// Swap both reservations in one server tick so neither ring exposes a
	// transient vacancy and the demoted actor keeps its original queue entry.
	AttackReservations[AttackSlotIndex] = PromotedRequester;
	WaitReservations[WaitSlotIndex] = Requester;
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	SetAttackEligibleTime(
		Requester,
		CurrentTime + FMath::Max(0.0f, AttackReentryCooldown));

	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s swapped stalled Attack owner %s with ready Wait candidate %s."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		*Requester->GetName(),
		*PromotedRequester->GetName());
	return true;
}

void UCombatEngagementSlotComponent::InitializeSlots()
{
	AttackReservations.SetNum(FMath::Max(1, AttackSlotCount));
	WaitReservations.SetNum(FMath::Max(1, WaitSlotCount));
	HoldingReservations.SetNum(FMath::Max(1, HoldingSlotCount));
}

void UCombatEngagementSlotComponent::UpdateEngagementAnchor(float DeltaTime)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const float SafeDeltaTime = FMath::Max(0.0f, DeltaTime);
	const float OwnerSpeed = Owner->GetVelocity().Size2D();
	const float StopSpeedThreshold = FMath::Max(0.0f, EngagementAnchorStopSpeedThreshold);
	const float ResumeSpeedThreshold = FMath::Max(StopSpeedThreshold, EngagementAnchorResumeSpeedThreshold);
	if (bEngagementAnchorRecentering)
	{
		if (OwnerSpeed >= ResumeSpeedThreshold)
		{
			bEngagementAnchorRecentering = false;
			EngagementAnchorStoppedElapsed = 0.0f;
		}
	}
	else if (OwnerSpeed <= StopSpeedThreshold)
	{
		EngagementAnchorStoppedElapsed += SafeDeltaTime;
		if (EngagementAnchorStoppedElapsed >= FMath::Max(0.0f, EngagementAnchorSettleDelay))
		{
			bEngagementAnchorRecentering = true;
		}
	}
	else
	{
		EngagementAnchorStoppedElapsed = 0.0f;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	FVector ToOwner = OwnerLocation - EngagementAnchorLocation;
	ToOwner.Z = 0.0f;
	const float Distance = ToOwner.Size2D();
	if (bEngagementAnchorRecentering)
	{
		const float SnapDistance = FMath::Max(0.0f, EngagementAnchorRecenterSnapDistance);
		const float RecenterDistance = FMath::Max(0.0f, EngagementAnchorRecenterSpeed) * SafeDeltaTime;
		if (Distance <= SnapDistance || RecenterDistance >= Distance)
		{
			EngagementAnchorLocation.X = OwnerLocation.X;
			EngagementAnchorLocation.Y = OwnerLocation.Y;
		}
		else if (!ToOwner.IsNearlyZero())
		{
			EngagementAnchorLocation += ToOwner.GetSafeNormal2D() * RecenterDistance;
		}
	}
	else
	{
		const float DeadZone = FMath::Max(0.0f, EngagementAnchorDeadZone);
		if (Distance > DeadZone && !ToOwner.IsNearlyZero())
		{
			const float FollowDistance = FMath::Min(
				Distance - DeadZone,
				FMath::Max(0.0f, EngagementAnchorFollowSpeed) * SafeDeltaTime);
			EngagementAnchorLocation += ToOwner.GetSafeNormal2D() * FollowDistance;
		}
	}
	EngagementAnchorLocation.Z = OwnerLocation.Z;
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

	for (TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		if (!Reservation.IsValid())
		{
			Reservation.Reset();
		}
	}

	for (int32 QueueIndex = EngagementQueue.Num() - 1; QueueIndex >= 0; --QueueIndex)
	{
		if (!EngagementQueue[QueueIndex].Requester.IsValid())
		{
			EngagementQueue.RemoveAtSwap(QueueIndex, 1, EAllowShrinking::No);
		}
	}

	if (EngagementQueue.IsEmpty())
	{
		bInitialFormationActive = true;
		LastRequesterRegistrationTime = 0.0f;
	}
}

void UCombatEngagementSlotComponent::RefreshInitialFormationPhase()
{
	if (!bInitialFormationActive || EngagementQueue.IsEmpty())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World
		|| World->GetTimeSeconds() - LastRequesterRegistrationTime < InitialFormationSettleTime)
	{
		return;
	}

	FinalizeInitialFormationAssignments();
	bInitialFormationActive = false;
	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s completed congestion-aware initial engagement formation assignment."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"));
}

void UCombatEngagementSlotComponent::FinalizeInitialFormationAssignments()
{
	for (TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		Reservation.Reset();
	}
	for (TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		Reservation.Reset();
	}
	for (TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		Reservation.Reset();
	}

	AActor* Requester = nullptr;
	int32 AttackSlotIndex = INDEX_NONE;
	while (FindBestInitialAttackAssignment(Requester, AttackSlotIndex))
	{
		AttackReservations[AttackSlotIndex] = Requester;
	}

	while (FindOldestPendingRequester(Requester))
	{
		if (!TryReserveSlot(Requester, EBHCombatSlotType::Wait))
		{
			break;
		}
	}

	while (FindOldestPendingRequester(Requester))
	{
		if (!TryReserveSlot(Requester, EBHCombatSlotType::Holding))
		{
			break;
		}
	}

	++FormationRevision;
}

void UCombatEngagementSlotComponent::RefreshPromotions()
{
	while (PromoteBestWaitReservationToAttack())
	{
	}

	while (PromoteOldestReservation(
		HoldingReservations,
		EBHCombatSlotType::Holding,
		EBHCombatSlotType::Wait))
	{
	}

	while (AssignOldestPendingRequesterToHolding())
	{
	}
}

bool UCombatEngagementSlotComponent::PromoteBestWaitReservationToAttack()
{
	int32 WaitSlotIndex = INDEX_NONE;
	int32 AttackSlotIndex = INDEX_NONE;
	if (!FindBestWaitAdmission(WaitSlotIndex, AttackSlotIndex))
	{
		return false;
	}

	AActor* Requester = WaitReservations[WaitSlotIndex].Get();
	if (!Requester)
	{
		return false;
	}

	WaitReservations[WaitSlotIndex].Reset();
	AttackReservations[AttackSlotIndex] = Requester;
	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s admitted %s from Wait to Attack slot %d by congestion-aware path cost."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		*Requester->GetName(),
		AttackSlotIndex);
	return true;
}

bool UCombatEngagementSlotComponent::PromoteOldestReservation(
	TArray<TWeakObjectPtr<AActor>>& SourceReservations,
	EBHCombatSlotType SourceType,
	EBHCombatSlotType DestinationType)
{
	int32 SourceIndex = INDEX_NONE;
	if (!FindOldestEligibleReservation(SourceReservations, SourceType, SourceIndex))
	{
		return false;
	}

	AActor* Requester = SourceReservations[SourceIndex].Get();
	if (!Requester)
	{
		SourceReservations[SourceIndex].Reset();
		return false;
	}

	SourceReservations[SourceIndex].Reset();
	float MaxDistanceFromOwner = -1.0f;
	if (DestinationType == EBHCombatSlotType::Attack)
	{
		MaxDistanceFromOwner = GetMaximumAttackSlotDistance(Requester);
	}

	if (!TryReserveSlot(Requester, DestinationType, MaxDistanceFromOwner))
	{
		SourceReservations[SourceIndex] = Requester;
		return false;
	}

	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s promoted %s from %s to %s."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		*Requester->GetName(),
		*StaticEnum<EBHCombatSlotType>()->GetNameStringByValue(static_cast<int64>(SourceType)),
		*StaticEnum<EBHCombatSlotType>()->GetNameStringByValue(static_cast<int64>(DestinationType)));
	return true;
}

bool UCombatEngagementSlotComponent::AssignOldestPendingRequesterToHolding()
{
	AActor* Requester = nullptr;
	if (!FindOldestPendingRequester(Requester))
	{
		return false;
	}

	return TryReserveSlot(Requester, EBHCombatSlotType::Holding);
}

bool UCombatEngagementSlotComponent::FindOldestEligibleReservation(
	const TArray<TWeakObjectPtr<AActor>>& Reservations,
	EBHCombatSlotType SlotType,
	int32& OutReservationIndex) const
{
	OutReservationIndex = INDEX_NONE;
	uint64 OldestSequence = TNumericLimits<uint64>::Max();
	for (int32 SlotIndex = 0; SlotIndex < Reservations.Num(); ++SlotIndex)
	{
		AActor* Requester = Reservations[SlotIndex].Get();
		if (!Requester)
		{
			continue;
		}

		FVector SlotLocation;
		if (!GetSlotWorldLocation(SlotType, SlotIndex, SlotLocation)
			|| FVector::DistSquared2D(Requester->GetActorLocation(), SlotLocation)
				> FMath::Square(PromotionArrivalRadius))
		{
			continue;
		}

		const uint64 Sequence = GetQueueSequence(Requester);
		if (OutReservationIndex == INDEX_NONE || Sequence < OldestSequence)
		{
			OldestSequence = Sequence;
			OutReservationIndex = SlotIndex;
		}
	}

	return OutReservationIndex != INDEX_NONE;
}

bool UCombatEngagementSlotComponent::FindBestWaitAdmission(
	int32& OutWaitSlotIndex,
	int32& OutAttackSlotIndex) const
{
	OutWaitSlotIndex = INDEX_NONE;
	OutAttackSlotIndex = INDEX_NONE;
	float BestPathScore = TNumericLimits<float>::Max();
	uint64 BestSequence = TNumericLimits<uint64>::Max();
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	for (int32 AttackSlotIndex = 0; AttackSlotIndex < AttackReservations.Num(); ++AttackSlotIndex)
	{
		if (AttackReservations[AttackSlotIndex].IsValid())
		{
			continue;
		}

		FVector AttackSlotLocation;
		if (!GetSlotWorldLocation(EBHCombatSlotType::Attack, AttackSlotIndex, AttackSlotLocation))
		{
			continue;
		}

		for (int32 WaitSlotIndex = 0; WaitSlotIndex < WaitReservations.Num(); ++WaitSlotIndex)
		{
			AActor* Requester = WaitReservations[WaitSlotIndex].Get();
			if (!Requester || CurrentTime < GetAttackEligibleTime(Requester))
			{
				continue;
			}

			FVector WaitSlotLocation;
			if (!GetSlotWorldLocation(EBHCombatSlotType::Wait, WaitSlotIndex, WaitSlotLocation)
				|| FVector::DistSquared2D(Requester->GetActorLocation(), WaitSlotLocation)
					> FMath::Square(PromotionArrivalRadius))
			{
				continue;
			}

			const float MaximumAttackSlotDistance = GetMaximumAttackSlotDistance(Requester);
			if (FVector::DistSquared2D(GetOwner()->GetActorLocation(), AttackSlotLocation)
				> FMath::Square(MaximumAttackSlotDistance))
			{
				continue;
			}

			float PathScore = 0.0f;
			if (!GetNavigationPathScore(Requester, AttackSlotLocation, PathScore))
			{
				continue;
			}

			const uint64 Sequence = GetQueueSequence(Requester);
			const bool bBetterPath = PathScore + 1.0f < BestPathScore;
			const bool bSamePathOlder = FMath::IsNearlyEqual(PathScore, BestPathScore, 1.0f)
				&& Sequence < BestSequence;
			if (OutWaitSlotIndex == INDEX_NONE || bBetterPath || bSamePathOlder)
			{
				BestPathScore = PathScore;
				BestSequence = Sequence;
				OutWaitSlotIndex = WaitSlotIndex;
				OutAttackSlotIndex = AttackSlotIndex;
			}
		}
	}

	return OutWaitSlotIndex != INDEX_NONE && OutAttackSlotIndex != INDEX_NONE;
}

bool UCombatEngagementSlotComponent::FindBestWaitAdmissionForAttackSlot(
	int32 AttackSlotIndex,
	int32& OutWaitSlotIndex) const
{
	OutWaitSlotIndex = INDEX_NONE;
	if (!AttackReservations.IsValidIndex(AttackSlotIndex))
	{
		return false;
	}

	FVector AttackSlotLocation;
	if (!GetSlotWorldLocation(EBHCombatSlotType::Attack, AttackSlotIndex, AttackSlotLocation))
	{
		return false;
	}

	float BestPathScore = TNumericLimits<float>::Max();
	uint64 BestSequence = TNumericLimits<uint64>::Max();
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	for (int32 WaitSlotIndex = 0; WaitSlotIndex < WaitReservations.Num(); ++WaitSlotIndex)
	{
		AActor* Requester = WaitReservations[WaitSlotIndex].Get();
		if (!Requester || CurrentTime < GetAttackEligibleTime(Requester))
		{
			continue;
		}

		FVector WaitSlotLocation;
		if (!GetSlotWorldLocation(EBHCombatSlotType::Wait, WaitSlotIndex, WaitSlotLocation)
			|| FVector::DistSquared2D(Requester->GetActorLocation(), WaitSlotLocation)
				> FMath::Square(PromotionArrivalRadius))
		{
			continue;
		}

		const float MaximumAttackSlotDistance = GetMaximumAttackSlotDistance(Requester);
		if (FVector::DistSquared2D(GetOwner()->GetActorLocation(), AttackSlotLocation)
			> FMath::Square(MaximumAttackSlotDistance))
		{
			continue;
		}

		float PathScore = 0.0f;
		if (!GetNavigationPathScore(Requester, AttackSlotLocation, PathScore))
		{
			continue;
		}

		const uint64 Sequence = GetQueueSequence(Requester);
		const bool bBetterPath = PathScore + 1.0f < BestPathScore;
		const bool bSamePathOlder = FMath::IsNearlyEqual(PathScore, BestPathScore, 1.0f)
			&& Sequence < BestSequence;
		if (OutWaitSlotIndex == INDEX_NONE || bBetterPath || bSamePathOlder)
		{
			BestPathScore = PathScore;
			BestSequence = Sequence;
			OutWaitSlotIndex = WaitSlotIndex;
		}
	}

	return OutWaitSlotIndex != INDEX_NONE;
}

bool UCombatEngagementSlotComponent::FindBestInitialAttackAssignment(
	AActor*& OutRequester,
	int32& OutAttackSlotIndex) const
{
	OutRequester = nullptr;
	OutAttackSlotIndex = INDEX_NONE;
	float BestPathScore = TNumericLimits<float>::Max();
	uint64 BestSequence = TNumericLimits<uint64>::Max();
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	for (int32 AttackSlotIndex = 0; AttackSlotIndex < AttackReservations.Num(); ++AttackSlotIndex)
	{
		if (AttackReservations[AttackSlotIndex].IsValid())
		{
			continue;
		}

		FVector AttackSlotLocation;
		if (!GetSlotWorldLocation(EBHCombatSlotType::Attack, AttackSlotIndex, AttackSlotLocation))
		{
			continue;
		}

		for (const FEngagementQueueEntry& Entry : EngagementQueue)
		{
			AActor* Requester = Entry.Requester.Get();
			if (!Requester || IsRequesterReserved(Requester) || CurrentTime < Entry.AttackEligibleTime)
			{
				continue;
			}

			const float MaximumAttackSlotDistance = GetMaximumAttackSlotDistance(Requester);
			if (FVector::DistSquared2D(GetOwner()->GetActorLocation(), AttackSlotLocation)
				> FMath::Square(MaximumAttackSlotDistance))
			{
				continue;
			}

			float PathScore = 0.0f;
			if (!GetNavigationPathScore(Requester, AttackSlotLocation, PathScore))
			{
				continue;
			}

			const bool bBetterPath = PathScore + 1.0f < BestPathScore;
			const bool bSamePathOlder = FMath::IsNearlyEqual(PathScore, BestPathScore, 1.0f)
				&& Entry.Sequence < BestSequence;
			if (!OutRequester || bBetterPath || bSamePathOlder)
			{
				BestPathScore = PathScore;
				BestSequence = Entry.Sequence;
				OutRequester = Requester;
				OutAttackSlotIndex = AttackSlotIndex;
			}
		}
	}

	return OutRequester != nullptr && OutAttackSlotIndex != INDEX_NONE;
}

bool UCombatEngagementSlotComponent::FindOldestPendingRequester(AActor*& OutRequester) const
{
	OutRequester = nullptr;
	uint64 OldestSequence = TNumericLimits<uint64>::Max();
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* Requester = Entry.Requester.Get();
		if (!Requester || IsRequesterReserved(Requester) || Entry.Sequence >= OldestSequence)
		{
			continue;
		}

		OldestSequence = Entry.Sequence;
		OutRequester = Requester;
	}

	return OutRequester != nullptr;
}

bool UCombatEngagementSlotComponent::FindPendingRequesterIndex(
	AActor* Requester,
	int32& OutPendingIndex) const
{
	OutPendingIndex = INDEX_NONE;
	const uint64 RequesterSequence = GetQueueSequence(Requester);
	if (RequesterSequence == TNumericLimits<uint64>::Max())
	{
		return false;
	}

	int32 PendingBeforeRequester = 0;
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* OtherRequester = Entry.Requester.Get();
		if (!OtherRequester || IsRequesterReserved(OtherRequester))
		{
			continue;
		}

		if (Entry.Sequence < RequesterSequence)
		{
			++PendingBeforeRequester;
		}
	}

	OutPendingIndex = PendingBeforeRequester;
	return true;
}

bool UCombatEngagementSlotComponent::IsRequesterReserved(AActor* Requester) const
{
	int32 IgnoredIndex = INDEX_NONE;
	return Requester
		&& (FindReservation(AttackReservations, Requester, IgnoredIndex)
			|| FindReservation(WaitReservations, Requester, IgnoredIndex)
			|| FindReservation(HoldingReservations, Requester, IgnoredIndex));
}

bool UCombatEngagementSlotComponent::HasAnyValidReservation(
	const TArray<TWeakObjectPtr<AActor>>& Reservations) const
{
	for (const TWeakObjectPtr<AActor>& Reservation : Reservations)
	{
		if (Reservation.IsValid())
		{
			return true;
		}
	}
	return false;
}

void UCombatEngagementSlotComponent::RegisterQueueRequester(AActor* Requester)
{
	if (!Requester)
	{
		return;
	}

	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (Entry.Requester.Get() == Requester)
		{
			return;
		}
	}

	FEngagementQueueEntry& Entry = EngagementQueue.AddDefaulted_GetRef();
	Entry.Requester = Requester;
	Entry.Sequence = NextQueueSequence++;
	if (const UWorld* World = GetWorld())
	{
		LastRequesterRegistrationTime = World->GetTimeSeconds();
	}
}

void UCombatEngagementSlotComponent::RemoveQueueRequester(AActor* Requester)
{
	for (int32 QueueIndex = EngagementQueue.Num() - 1; QueueIndex >= 0; --QueueIndex)
	{
		if (EngagementQueue[QueueIndex].Requester.Get() == Requester)
		{
			EngagementQueue.RemoveAtSwap(QueueIndex, 1, EAllowShrinking::No);
		}
	}
}

uint64 UCombatEngagementSlotComponent::GetQueueSequence(AActor* Requester) const
{
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (Entry.Requester.Get() == Requester)
		{
			return Entry.Sequence;
		}
	}
	return TNumericLimits<uint64>::Max();
}

float UCombatEngagementSlotComponent::GetAttackEligibleTime(AActor* Requester) const
{
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (Entry.Requester.Get() == Requester)
		{
			return Entry.AttackEligibleTime;
		}
	}
	return TNumericLimits<float>::Max();
}

void UCombatEngagementSlotComponent::SetAttackEligibleTime(AActor* Requester, float EligibleTime)
{
	for (FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (Entry.Requester.Get() == Requester)
		{
			Entry.AttackEligibleTime = FMath::Max(0.0f, EligibleTime);
			return;
		}
	}
}

bool UCombatEngagementSlotComponent::IsInitialFormationActive() const
{
	return bInitialFormationActive;
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
	ReformRingReservations(HoldingReservations, EBHCombatSlotType::Holding);
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
		AActor* Requester = Reservations[SlotIndex].Get();
		const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
		const bool bKeepLockedAttackSlot = SlotType == EBHCombatSlotType::Attack
			&& Enemy
			&& (Enemy->GetCombatState() == EBHEnemyCombatState::Attacking
				|| Enemy->GetCombatState() == EBHEnemyCombatState::Recovering);
		if (bKeepLockedAttackSlot)
		{
			continue;
		}

		if (Requester)
		{
			Requesters.Add(Requester);
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

float UCombatEngagementSlotComponent::GetMaximumAttackSlotDistance(AActor* Requester) const
{
	const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
	if (!Enemy)
	{
		return 0.0f;
	}

	float ArrivalTolerance = 20.0f;
	if (const ABHCrowdEnemyAIController* CrowdController = Cast<ABHCrowdEnemyAIController>(Enemy->GetController()))
	{
		ArrivalTolerance = CrowdController->GetSlotAcceptanceRadius();
	}

	return FMath::Max(0.0f, Enemy->GetAttackStartRange() - FMath::Max(0.0f, ArrivalTolerance));
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
	case EBHCombatSlotType::Holding:
		Reservations = &HoldingReservations;
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
	int32 SlotCount = 0;
	float RingRadius = 0.0f;
	float AngleOffset = 0.0f;
	switch (SlotType)
	{
	case EBHCombatSlotType::Attack:
		SlotCount = AttackReservations.Num();
		RingRadius = AttackRingRadius;
		AngleOffset = AttackRingAngleOffset;
		break;
	case EBHCombatSlotType::Wait:
		SlotCount = WaitReservations.Num();
		RingRadius = WaitRingRadius;
		AngleOffset = WaitRingAngleOffset;
		break;
	case EBHCombatSlotType::Holding:
		SlotCount = HoldingReservations.Num();
		RingRadius = HoldingRingRadius;
		AngleOffset = HoldingRingAngleOffset;
		break;
	default:
		return false;
	}
	if (!Owner || SlotType == EBHCombatSlotType::None || !FMath::IsWithin(SlotIndex, 0, SlotCount))
	{
		return false;
	}

	const float AngleDegrees = AngleOffset + (360.0f * static_cast<float>(SlotIndex) / static_cast<float>(SlotCount));
	const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
	const FVector RingDirection(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0f);
	const FVector RingCenter = SlotType == EBHCombatSlotType::Attack
		? Owner->GetActorLocation()
		: EngagementAnchorLocation;
	const FVector DesiredLocation = RingCenter + RingDirection * RingRadius;

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

bool UCombatEngagementSlotComponent::GetNavigationPathScore(
	AActor* Requester,
	const FVector& Destination,
	float& OutPathScore) const
{
	OutPathScore = 0.0f;
	if (!Requester || !GetWorld())
	{
		return false;
	}

	UNavigationPath* NavigationPath = UNavigationSystemV1::FindPathToLocationSynchronously(
		GetWorld(),
		Requester->GetActorLocation(),
		Destination,
		Requester);
	if (!NavigationPath || !NavigationPath->IsValid() || NavigationPath->IsPartial())
	{
		return false;
	}

	const float PathLength = NavigationPath->GetPathLength();
	if (PathLength < 0.0f)
	{
		return false;
	}

	OutPathScore = PathLength + CalculatePathCongestionPenalty(Requester, NavigationPath->PathPoints);
	return true;
}

float UCombatEngagementSlotComponent::CalculatePathCongestionPenalty(
	AActor* Requester,
	const TArray<FVector>& PathPoints) const
{
	if (!Requester || PathPoints.Num() < 2
		|| AdmissionCongestionRadius <= 0.0f
		|| AdmissionCongestionPenaltyPerAgent <= 0.0f)
	{
		return 0.0f;
	}

	const float CongestionRadiusSquared = FMath::Square(AdmissionCongestionRadius);
	int32 NearbyAgentCount = 0;
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* OtherRequester = Entry.Requester.Get();
		if (!OtherRequester || OtherRequester == Requester)
		{
			continue;
		}
		const ABHEnemy* RequesterEnemy = Cast<ABHEnemy>(Requester);
		const ABHEnemy* OtherEnemy = Cast<ABHEnemy>(OtherRequester);
		const UCapsuleComponent* RequesterCapsule = RequesterEnemy
			? RequesterEnemy->GetCapsuleComponent()
			: nullptr;
		const UCapsuleComponent* OtherCapsule = OtherEnemy
			? OtherEnemy->GetCapsuleComponent()
			: nullptr;
		const float SameLayerTolerance = RequesterCapsule && OtherCapsule
			? FMath::Max(
				RequesterCapsule->GetScaledCapsuleHalfHeight(),
				OtherCapsule->GetScaledCapsuleHalfHeight())
			: NavProjectionExtent.Z;
		if (FMath::Abs(Requester->GetActorLocation().Z - OtherRequester->GetActorLocation().Z)
			> SameLayerTolerance)
		{
			continue;
		}

		const FVector2D OtherLocation(OtherRequester->GetActorLocation());
		bool bNearPath = false;
		for (int32 PointIndex = 1; PointIndex < PathPoints.Num(); ++PointIndex)
		{
			const FVector2D SegmentStart(PathPoints[PointIndex - 1]);
			const FVector2D SegmentEnd(PathPoints[PointIndex]);
			const FVector2D Segment = SegmentEnd - SegmentStart;
			const float SegmentLengthSquared = Segment.SizeSquared();
			const float ClosestAlpha = SegmentLengthSquared <= UE_KINDA_SMALL_NUMBER
				? 0.0f
				: FMath::Clamp(
					FVector2D::DotProduct(OtherLocation - SegmentStart, Segment) / SegmentLengthSquared,
					0.0f,
					1.0f);
			const FVector2D ClosestPoint = SegmentStart + Segment * ClosestAlpha;
			if (FVector2D::DistSquared(OtherLocation, ClosestPoint) <= CongestionRadiusSquared)
			{
				bNearPath = true;
				break;
			}
		}

		if (bNearPath)
		{
			++NearbyAgentCount;
		}
	}

	return static_cast<float>(NearbyAgentCount) * AdmissionCongestionPenaltyPerAgent;
}

void UCombatEngagementSlotComponent::UpdateDebugMetrics()
{
	CurrentSpacingViolationCount = 0;
	CurrentAttackingEnemyCount = 0;
	CurrentNonAttackSlotAttackerCount = 0;

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
			if (Controller->GetCurrentCombatSlotType() != EBHCombatSlotType::Attack)
			{
				++CurrentNonAttackSlotAttackerCount;
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

	const FColor AnchorDebugColor = bEngagementAnchorRecentering ? FColor::Yellow : FColor::Cyan;
	DrawDebugSphere(
		World,
		EngagementAnchorLocation,
		18.0f,
		10,
		AnchorDebugColor,
		false,
		0.12f,
		0,
		2.0f);
	DrawDebugLine(
		World,
		Owner->GetActorLocation(),
		EngagementAnchorLocation,
		AnchorDebugColor,
		false,
		0.12f,
		0,
		1.5f);

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

	for (int32 SlotIndex = 0; SlotIndex < HoldingReservations.Num(); ++SlotIndex)
	{
		FVector SlotLocation;
		if (GetSlotWorldLocation(EBHCombatSlotType::Holding, SlotIndex, SlotLocation))
		{
			const FColor Color = HoldingReservations[SlotIndex].IsValid()
				? FColor::Purple
				: FColor::Blue;
			DrawDebugSphere(World, SlotLocation, 13.0f, 8, Color, false, 0.12f, 0, 1.25f);
		}
	}

	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* Requester = Entry.Requester.Get();
		if (!Requester || IsRequesterReserved(Requester))
		{
			continue;
		}

		int32 PendingIndex = INDEX_NONE;
		FVector PendingLocation;
		if (GetPendingWaitLocation(Requester, PendingIndex, PendingLocation))
		{
			DrawDebugSphere(
				World,
				PendingLocation,
				10.0f,
				8,
				FColor(0, 200, 120),
				false,
				0.12f,
				0,
				1.0f);
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

	int32 OccupiedHoldingSlots = 0;
	for (const TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		OccupiedHoldingSlots += Reservation.IsValid() ? 1 : 0;
	}

	int32 PendingRequesterCount = 0;
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* Requester = Entry.Requester.Get();
		PendingRequesterCount += Requester && !IsRequesterReserved(Requester) ? 1 : 0;
	}

	const FString DebugText = FString::Printf(
		TEXT("Slots A:%d/%d W:%d/%d H:%d/%d Q:%d | Phase:%s | Reform:%d | Attacking:%d NonAttack:%d | Spacing:%d Peak:%d"),
		OccupiedAttackSlots,
		AttackReservations.Num(),
		OccupiedWaitSlots,
		WaitReservations.Num(),
		OccupiedHoldingSlots,
		HoldingReservations.Num(),
		PendingRequesterCount,
		IsInitialFormationActive() ? TEXT("Initial") : TEXT("Runtime"),
		FormationRevision,
		CurrentAttackingEnemyCount,
		CurrentNonAttackSlotAttackerCount,
		CurrentSpacingViolationCount,
		PeakSpacingViolationCount);
	DrawDebugString(
		World,
		Owner->GetActorLocation() + FVector(0.0f, 0.0f, 170.0f),
		DebugText,
		nullptr,
		CurrentNonAttackSlotAttackerCount > 0 ? FColor::Red : FColor::White,
		0.12f,
		false,
		1.1f);
}
