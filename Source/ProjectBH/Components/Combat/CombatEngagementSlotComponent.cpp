// Copyright ProjectBH. All Rights Reserved.

#include "CombatEngagementSlotComponent.h"

#include "../../AI/BHCrowdEnemyAIController.h"
#include "../../Debug/BHDebugDraw.h"
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
	SpaceProbeEndpoints.Reset();
	SpaceProbeBlocked.Reset();

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
	UpdateCombatSpaceAnalysis(DeltaTime);
	RefreshCorridorFormationCapacity();
	RefreshPocketFormationCapacity();
	UpdateEngagementAnchor(DeltaTime);
	RefreshInitialFormationPhase();
	if (!IsInitialFormationActive())
	{
		RefreshPromotions();
	}
	TryReformFormation();
	if (!BHDebugDraw::IsSlotsEnabled(bDrawDebugSlots))
	{
		return;
	}

	UpdateDebugMetrics();
	DrawDebugSlots();
}

void UCombatEngagementSlotComponent::UpdateCombatSpaceAnalysis(float DeltaTime)
{
	if (!bEnableCombatSpaceAnalysis)
	{
		bHasValidSpaceAnalysis = false;
		SpaceAnalysisElapsed = 0.0f;
		SpaceModeTransitionElapsed = 0.0f;
		return;
	}

	SpaceAnalysisElapsed += DeltaTime;
	const float EffectiveInterval = FMath::Max(0.05f, SpaceAnalysisInterval);
	if (SpaceAnalysisElapsed < EffectiveInterval)
	{
		return;
	}

	const float SampleDeltaTime = SpaceAnalysisElapsed;
	SpaceAnalysisElapsed = 0.0f;
	AnalyzeCombatSpace(SampleDeltaTime);
}

void UCombatEngagementSlotComponent::AnalyzeCombatSpace(float SampleDeltaTime)
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	FVector ProjectedOrigin;
	if (!Owner || !NavigationSystem || !ProjectToNavigation(Owner->GetActorLocation(), ProjectedOrigin))
	{
		bHasValidSpaceAnalysis = false;
		CandidateSpaceMode = CurrentSpaceMode;
		SpaceModeTransitionElapsed = 0.0f;
		SpaceProbeEndpoints.Reset();
		SpaceProbeBlocked.Reset();
		CorridorAxisProbeIndex = INDEX_NONE;
		CorridorWidthProbeIndex = INDEX_NONE;
		EstimatedPocketOpenArc = 0.0f;
		EstimatedPocketBlockedFraction = 0.0f;
		return;
	}

	const int32 RequestedProbeCount = FMath::Clamp(SpaceProbeCount, 8, 64);
	const int32 EffectiveProbeCount = FMath::Min(64, ((RequestedProbeCount + 3) / 4) * 4);
	const int32 HalfProbeCount = EffectiveProbeCount / 2;
	const int32 QuarterProbeCount = EffectiveProbeCount / 4;
	const float EffectiveProbeDistance = FMath::Max(100.0f, SpaceProbeDistance);

	SpaceProbeOrigin = ProjectedOrigin;
	SpaceProbeEndpoints.SetNum(EffectiveProbeCount);
	SpaceProbeBlocked.SetNumZeroed(EffectiveProbeCount);
	TArray<float, TInlineAllocator<64>> ProbeDistances;
	ProbeDistances.SetNumZeroed(EffectiveProbeCount);

	for (int32 ProbeIndex = 0; ProbeIndex < EffectiveProbeCount; ++ProbeIndex)
	{
		const float AngleRadians = 2.0f * UE_PI
			* static_cast<float>(ProbeIndex) / static_cast<float>(EffectiveProbeCount);
		const FVector ProbeDirection(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0f);
		const FVector ProbeEnd = ProjectedOrigin + ProbeDirection * EffectiveProbeDistance;
		FVector HitLocation = ProbeEnd;
		const bool bBlocked = NavigationSystem->NavigationRaycast(
			GetWorld(),
			ProjectedOrigin,
			ProbeEnd,
			HitLocation);
		const FVector ReachLocation = bBlocked ? HitLocation : ProbeEnd;

		SpaceProbeEndpoints[ProbeIndex] = ReachLocation;
		SpaceProbeBlocked[ProbeIndex] = bBlocked ? 1 : 0;
		ProbeDistances[ProbeIndex] = FMath::Min(
			EffectiveProbeDistance,
			FVector::Dist2D(ProjectedOrigin, ReachLocation));
	}

	CorridorAxisProbeIndex = 0;
	EstimatedCorridorAxisLength = -1.0f;
	for (int32 PairIndex = 0; PairIndex < HalfProbeCount; ++PairIndex)
	{
		const float PairLength = ProbeDistances[PairIndex]
			+ ProbeDistances[PairIndex + HalfProbeCount];
		if (PairLength > EstimatedCorridorAxisLength)
		{
			EstimatedCorridorAxisLength = PairLength;
			CorridorAxisProbeIndex = PairIndex;
		}
	}

	CorridorWidthProbeIndex = (CorridorAxisProbeIndex + QuarterProbeCount) % HalfProbeCount;
	EstimatedCorridorWidth = ProbeDistances[CorridorWidthProbeIndex]
		+ ProbeDistances[CorridorWidthProbeIndex + HalfProbeCount];
	EstimatedCorridorAspectRatio = EstimatedCorridorAxisLength
		/ FMath::Max(1.0f, EstimatedCorridorWidth);

	const float AxisAngleRadians = 2.0f * UE_PI
		* static_cast<float>(CorridorAxisProbeIndex) / static_cast<float>(EffectiveProbeCount);
	FVector NewCorridorAxis(FMath::Cos(AxisAngleRadians), FMath::Sin(AxisAngleRadians), 0.0f);
	if (FVector::DotProduct(NewCorridorAxis, EstimatedCorridorAxis) < 0.0f)
	{
		NewCorridorAxis *= -1.0f;
	}
	EstimatedCorridorAxis = NewCorridorAxis;

	int32 NearbyBlockedProbeCount = 0;
	const float NearbyWallDistance = FMath::Max(0.0f, PocketNearbyWallDistance);
	for (int32 ProbeIndex = 0; ProbeIndex < EffectiveProbeCount; ++ProbeIndex)
	{
		if (SpaceProbeBlocked[ProbeIndex] != 0
			&& ProbeDistances[ProbeIndex] <= NearbyWallDistance)
		{
			++NearbyBlockedProbeCount;
		}
	}
	EstimatedPocketBlockedFraction = static_cast<float>(NearbyBlockedProbeCount)
		/ static_cast<float>(EffectiveProbeCount);

	const float OpenProbeClearance = FMath::Clamp(
		PocketOpenProbeClearance,
		100.0f,
		EffectiveProbeDistance);
	int32 LongestOpenArcStart = INDEX_NONE;
	int32 LongestOpenArcProbeCount = 0;
	for (int32 StartIndex = 0; StartIndex < EffectiveProbeCount; ++StartIndex)
	{
		int32 OpenProbeCount = 0;
		while (OpenProbeCount < EffectiveProbeCount
			&& ProbeDistances[(StartIndex + OpenProbeCount) % EffectiveProbeCount] >= OpenProbeClearance)
		{
			++OpenProbeCount;
		}
		if (OpenProbeCount > LongestOpenArcProbeCount)
		{
			LongestOpenArcStart = StartIndex;
			LongestOpenArcProbeCount = OpenProbeCount;
		}
	}

	const float ProbeAngleStep = 360.0f / static_cast<float>(EffectiveProbeCount);
	EstimatedPocketOpenArc = LongestOpenArcProbeCount > 1
		? static_cast<float>(LongestOpenArcProbeCount - 1) * ProbeAngleStep
		: 0.0f;
	if (LongestOpenArcStart != INDEX_NONE && LongestOpenArcProbeCount > 0)
	{
		const float OpenArcCenterAngle = ProbeAngleStep
			* (static_cast<float>(LongestOpenArcStart)
				+ 0.5f * static_cast<float>(LongestOpenArcProbeCount - 1));
		EstimatedPocketOpenDirection = FVector(
			FMath::Cos(FMath::DegreesToRadians(OpenArcCenterAngle)),
			FMath::Sin(FMath::DegreesToRadians(OpenArcCenterAngle)),
			0.0f);
	}
	bHasValidSpaceAnalysis = true;

	const float EnterWidth = FMath::Max(0.0f, CorridorEnterMaxWidth);
	const float ExitWidth = FMath::Max(EnterWidth, CorridorExitMinWidth);
	const float EnterRatio = FMath::Max(1.0f, CorridorEnterAspectRatio);
	const float ExitRatio = FMath::Clamp(CorridorExitAspectRatio, 1.0f, EnterRatio);
	const bool bCorridorEvidence = EstimatedCorridorAxisLength >= CorridorMinimumAxisLength
		&& EstimatedCorridorWidth <= EnterWidth
		&& EstimatedCorridorAspectRatio >= EnterRatio;
	const bool bOpenEvidence = EstimatedCorridorAxisLength < CorridorMinimumAxisLength
		|| EstimatedCorridorWidth >= ExitWidth
		|| EstimatedCorridorAspectRatio <= ExitRatio;
	const float RequiredPocketBlockedFraction = CurrentSpaceMode == EBHCombatSpaceMode::Pocket
		? FMath::Clamp(PocketExitBlockedFraction, 0.0f, 1.0f)
		: FMath::Clamp(PocketEnterBlockedFraction, 0.0f, 1.0f);
	const float RequiredPocketOpenArc = CurrentSpaceMode == EBHCombatSpaceMode::Pocket
		? FMath::Clamp(PocketExitMinimumOpenArc, 0.0f, 360.0f)
		: FMath::Clamp(PocketEnterMinimumOpenArc, 0.0f, 360.0f);
	const bool bPocketEvidence = EstimatedPocketBlockedFraction >= RequiredPocketBlockedFraction
		&& EstimatedPocketOpenArc >= RequiredPocketOpenArc;

	if (bPocketEvidence)
	{
		CandidateSpaceMode = EBHCombatSpaceMode::Pocket;
	}
	else if (bCorridorEvidence)
	{
		CandidateSpaceMode = EBHCombatSpaceMode::Corridor;
	}
	else if (bOpenEvidence)
	{
		CandidateSpaceMode = EBHCombatSpaceMode::Open;
	}
	else
	{
		// The gap between enter/exit thresholds is an intentional hysteresis band.
		CandidateSpaceMode = CurrentSpaceMode;
	}

	if (CandidateSpaceMode == CurrentSpaceMode)
	{
		SpaceModeTransitionElapsed = 0.0f;
		UpdateCorridorFormationDirection();
		UpdatePocketFormationDirection();
		return;
	}

	SpaceModeTransitionElapsed += SampleDeltaTime;
	float RequiredDuration = FMath::Max(0.0f, CorridorExitDuration);
	if (CandidateSpaceMode == EBHCombatSpaceMode::Pocket)
	{
		RequiredDuration = FMath::Max(0.0f, PocketEnterDuration);
	}
	else if (CurrentSpaceMode == EBHCombatSpaceMode::Pocket)
	{
		RequiredDuration = FMath::Max(0.0f, PocketExitDuration);
	}
	else if (CandidateSpaceMode == EBHCombatSpaceMode::Corridor)
	{
		RequiredDuration = FMath::Max(0.0f, CorridorEnterDuration);
	}
	if (SpaceModeTransitionElapsed >= RequiredDuration)
	{
		const EBHCombatSpaceMode PreviousMode = CurrentSpaceMode;
		CurrentSpaceMode = CandidateSpaceMode;
		SpaceModeTransitionElapsed = 0.0f;
		HandleCombatSpaceModeChanged(PreviousMode);
	}
}

int32 UCombatEngagementSlotComponent::GetActiveAttackSlotCount() const
{
	if (IsCorridorFormationActive())
	{
		return FMath::Clamp(ActiveCorridorAttackSlotCount, 1, AttackReservations.Num());
	}
	if (IsPocketFormationActive())
	{
		return FMath::Clamp(ActivePocketAttackSlotCount, 1, AttackReservations.Num());
	}
	return AttackReservations.Num();
}

int32 UCombatEngagementSlotComponent::GetCorridorLaneForRequester(AActor* Requester) const
{
	return IsCorridorFormationActive() ? GetCorridorLaneIndex(Requester) : INDEX_NONE;
}

int32 UCombatEngagementSlotComponent::GetCorridorSideForRequester(AActor* Requester) const
{
	return IsCorridorFormationActive() ? GetCorridorSideIndex(Requester) : INDEX_NONE;
}

void UCombatEngagementSlotComponent::HandleCombatSpaceModeChanged(EBHCombatSpaceMode PreviousMode)
{
	if (PreviousMode == CurrentSpaceMode)
	{
		return;
	}
	const AActor* Owner = GetOwner();

	if (CurrentSpaceMode == EBHCombatSpaceMode::Corridor)
	{
		CorridorFormationRearDirection = ResolveCorridorRearDirection(EstimatedCorridorAxis);
		LastNotifiedCorridorDirection = CorridorFormationRearDirection;
		ActiveCorridorLaneCount = CalculateCorridorLaneCount(EstimatedCorridorWidth);
		AssignCorridorSideIndices(true);
		DesiredCorridorAttackSlotCount = FMath::Min(
			AttackReservations.Num(),
			2 * CalculateCorridorAttackSlotCount(EstimatedCorridorWidth));
		ActiveCorridorAttackSlotCount = FMath::Max(
			DesiredCorridorAttackSlotCount,
			GetLockedAttackReservationCount());
		ReconcileCorridorAttackReservations();
	}
	else if (CurrentSpaceMode == EBHCombatSpaceMode::Pocket)
	{
		for (FEngagementQueueEntry& Entry : EngagementQueue)
		{
			Entry.CorridorSideIndex = INDEX_NONE;
		}
		PocketFormationDirection = EstimatedPocketOpenDirection.GetSafeNormal2D();
		if (PocketFormationDirection.IsNearlyZero())
		{
			PocketFormationDirection = Owner ? Owner->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
		}
		LastNotifiedPocketDirection = PocketFormationDirection;
		ActiveCorridorLaneCount = 1;
		ActiveCorridorAttackSlotCount = 1;
		DesiredCorridorAttackSlotCount = 1;
		DesiredPocketAttackSlotCount = CalculatePocketAttackSlotCount();
		const int32 PreviousPocketAttackSlotCount = FMath::Max(1, AttackReservations.Num());
		ActivePocketAttackSlotCount = FMath::Clamp(
			FMath::Max(DesiredPocketAttackSlotCount, GetLockedAttackReservationCount()),
			1,
			FMath::Max(1, AttackReservations.Num()));
		if (!ReconcilePocketAttackReservations())
		{
			ActivePocketAttackSlotCount = PreviousPocketAttackSlotCount;
			UE_LOG(
				LogProjectBH,
				Warning,
				TEXT("%s kept its previous Pocket Attack reservations because the new active row could not be projected."),
				GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"));
		}
	}
	else
	{
		for (FEngagementQueueEntry& Entry : EngagementQueue)
		{
			Entry.CorridorSideIndex = INDEX_NONE;
		}
		ActiveCorridorLaneCount = 1;
		ActiveCorridorAttackSlotCount = 1;
		DesiredCorridorAttackSlotCount = 1;
		ActivePocketAttackSlotCount = 1;
		DesiredPocketAttackSlotCount = 1;
	}

	++FormationRevision;
	NotifyAllReservedRequestersSlotChanged();
	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s changed combat space mode from %s to %s. Corridor sides:%d lanes:%d active attacks:%d revision:%d"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		*StaticEnum<EBHCombatSpaceMode>()->GetNameStringByValue(static_cast<int64>(PreviousMode)),
		*StaticEnum<EBHCombatSpaceMode>()->GetNameStringByValue(static_cast<int64>(CurrentSpaceMode)),
		IsCorridorFormationActive() ? 2 : 0,
		ActiveCorridorLaneCount,
		GetActiveAttackSlotCount(),
		FormationRevision);
}

void UCombatEngagementSlotComponent::UpdateCorridorFormationDirection()
{
	if (!IsCorridorFormationActive())
	{
		return;
	}

	FVector AlignedAxis = EstimatedCorridorAxis.GetSafeNormal2D();
	if (AlignedAxis.IsNearlyZero())
	{
		return;
	}
	if (FVector::DotProduct(AlignedAxis, CorridorFormationRearDirection) < 0.0f)
	{
		AlignedAxis *= -1.0f;
	}

	const float FollowAlpha = FMath::Clamp(CorridorAxisFollowAlpha, 0.0f, 1.0f);
	CorridorFormationRearDirection = FMath::Lerp(
		CorridorFormationRearDirection,
		AlignedAxis,
		FollowAlpha).GetSafeNormal2D();
	if (CorridorFormationRearDirection.IsNearlyZero())
	{
		CorridorFormationRearDirection = AlignedAxis;
	}

	const float NotifyDotThreshold = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(CorridorFormationRepathAngle, 1.0f, 90.0f)));
	if (FVector::DotProduct(CorridorFormationRearDirection, LastNotifiedCorridorDirection)
		<= NotifyDotThreshold)
	{
		LastNotifiedCorridorDirection = CorridorFormationRearDirection;
		++FormationRevision;
		NotifyAllReservedRequestersSlotChanged();
	}
}

void UCombatEngagementSlotComponent::UpdatePocketFormationDirection()
{
	if (!IsPocketFormationActive())
	{
		return;
	}

	FVector OpenDirection = EstimatedPocketOpenDirection.GetSafeNormal2D();
	if (OpenDirection.IsNearlyZero())
	{
		return;
	}

	const float FollowAlpha = FMath::Clamp(PocketDirectionFollowAlpha, 0.0f, 1.0f);
	PocketFormationDirection = FMath::Lerp(
		PocketFormationDirection,
		OpenDirection,
		FollowAlpha).GetSafeNormal2D();
	if (PocketFormationDirection.IsNearlyZero())
	{
		PocketFormationDirection = OpenDirection;
	}

	const float NotifyDotThreshold = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(PocketFormationRepathAngle, 1.0f, 90.0f)));
	if (FVector::DotProduct(PocketFormationDirection, LastNotifiedPocketDirection)
		<= NotifyDotThreshold)
	{
		LastNotifiedPocketDirection = PocketFormationDirection;
		++FormationRevision;
		NotifyAllReservedRequestersSlotChanged();
	}
}

void UCombatEngagementSlotComponent::RefreshCorridorFormationCapacity()
{
	if (!IsCorridorFormationActive())
	{
		return;
	}

	const int32 RequiredAttackCount = FMath::Clamp(
		FMath::Max(DesiredCorridorAttackSlotCount, GetLockedAttackReservationCount()),
		1,
		FMath::Max(1, AttackReservations.Num()));
	if (RequiredAttackCount == ActiveCorridorAttackSlotCount)
	{
		return;
	}

	ActiveCorridorAttackSlotCount = RequiredAttackCount;
	ReconcileCorridorAttackReservations();
	++FormationRevision;
	NotifyAllReservedRequestersSlotChanged();
}

void UCombatEngagementSlotComponent::RefreshPocketFormationCapacity()
{
	if (!IsPocketFormationActive())
	{
		return;
	}

	DesiredPocketAttackSlotCount = CalculatePocketAttackSlotCount();
	const int32 RequiredAttackCount = FMath::Clamp(
		FMath::Max(DesiredPocketAttackSlotCount, GetLockedAttackReservationCount()),
		1,
		FMath::Max(1, AttackReservations.Num()));
	if (RequiredAttackCount == ActivePocketAttackSlotCount)
	{
		return;
	}

	const int32 PreviousAttackCount = ActivePocketAttackSlotCount;
	ActivePocketAttackSlotCount = RequiredAttackCount;
	if (!ReconcilePocketAttackReservations())
	{
		ActivePocketAttackSlotCount = PreviousAttackCount;
		return;
	}
	++FormationRevision;
	NotifyAllReservedRequestersSlotChanged();
}

void UCombatEngagementSlotComponent::ReconcileCorridorAttackReservations()
{
	if (!IsCorridorFormationActive())
	{
		return;
	}

	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<8>> LockedRequesters;
	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<8>> OtherRequesters;
	for (TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		AActor* Requester = Reservation.Get();
		if (Requester)
		{
			const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
			const bool bLocked = Enemy
				&& (Enemy->GetCombatState() == EBHEnemyCombatState::Attacking
					|| Enemy->GetCombatState() == EBHEnemyCombatState::Recovering);
			(bLocked ? LockedRequesters : OtherRequesters).Add(Requester);
		}
		Reservation.Reset();
	}

	for (const TWeakObjectPtr<AActor>& RequesterPtr : LockedRequesters)
	{
		AActor* Requester = RequesterPtr.Get();
		if (!Requester)
		{
			continue;
		}

		const int32 PreferredChannel = GetCorridorQueueChannelIndex(Requester);
		int32 ReservedIndex = INDEX_NONE;
		for (int32 SlotIndex = 0; SlotIndex < ActiveCorridorAttackSlotCount; ++SlotIndex)
		{
			if (!AttackReservations[SlotIndex].IsValid()
				&& GetCorridorAttackSlotChannelIndex(SlotIndex) == PreferredChannel)
			{
				ReservedIndex = SlotIndex;
				break;
			}
		}
		if (ReservedIndex == INDEX_NONE)
		{
			for (int32 SlotIndex = 0; SlotIndex < ActiveCorridorAttackSlotCount; ++SlotIndex)
			{
				if (!AttackReservations[SlotIndex].IsValid())
				{
					ReservedIndex = SlotIndex;
					break;
				}
			}
		}
		if (AttackReservations.IsValidIndex(ReservedIndex))
		{
			AttackReservations[ReservedIndex] = Requester;
		}
	}

	for (int32 SlotIndex = 0;
		SlotIndex < ActiveCorridorAttackSlotCount && !OtherRequesters.IsEmpty();
		++SlotIndex)
	{
		if (AttackReservations[SlotIndex].IsValid())
		{
			continue;
		}

		int32 BestRequesterIndex = INDEX_NONE;
		float BestDistanceSquared = TNumericLimits<float>::Max();
		FVector SlotLocation;
		if (!GetCorridorSlotWorldLocation(EBHCombatSlotType::Attack, SlotIndex, SlotLocation))
		{
			continue;
		}
		for (int32 RequesterIndex = 0; RequesterIndex < OtherRequesters.Num(); ++RequesterIndex)
		{
			AActor* Requester = OtherRequesters[RequesterIndex].Get();
			if (!Requester
				|| GetCorridorQueueChannelIndex(Requester)
					!= GetCorridorAttackSlotChannelIndex(SlotIndex))
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared2D(Requester->GetActorLocation(), SlotLocation);
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				BestRequesterIndex = RequesterIndex;
			}
		}
		if (BestRequesterIndex == INDEX_NONE)
		{
			continue;
		}

		AttackReservations[SlotIndex] = OtherRequesters[BestRequesterIndex];
		OtherRequesters.RemoveAtSwap(BestRequesterIndex, 1, EAllowShrinking::No);
	}

	for (const TWeakObjectPtr<AActor>& RequesterPtr : OtherRequesters)
	{
		AActor* Requester = RequesterPtr.Get();
		if (!Requester)
		{
			continue;
		}

		if (!TryReserveSlot(Requester, EBHCombatSlotType::Wait)
			&& !TryReserveSlot(Requester, EBHCombatSlotType::Holding))
		{
			// The central queue registration remains valid, so this requester
			// becomes Pending and can be promoted without losing its order.
		}
		NotifyRequesterSlotChanged(Requester);
	}
	RepackAllCorridorQueueLayers(true);
}

bool UCombatEngagementSlotComponent::ReconcilePocketAttackReservations()
{
	if (!IsPocketFormationActive())
	{
		return false;
	}

	// Validate the complete active row before touching live reservations. A transient
	// NavMesh projection failure must not make an attacking owner lose its slot.
	for (int32 SlotIndex = 0; SlotIndex < GetActiveAttackSlotCount(); ++SlotIndex)
	{
		FVector SlotLocation;
		if (!GetPocketSlotWorldLocation(
			EBHCombatSlotType::Attack,
			SlotIndex,
			SlotLocation))
		{
			return false;
		}
	}

	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<8>> LockedRequesters;
	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<8>> OtherRequesters;
	TSet<AActor*> SeenRequesters;
	const TArray<TWeakObjectPtr<AActor>> PreviousAttackReservations = AttackReservations;
	for (TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		AActor* Requester = Reservation.Get();
		if (Requester && !SeenRequesters.Contains(Requester))
		{
			SeenRequesters.Add(Requester);
			const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
			const bool bLocked = Enemy
				&& (Enemy->GetCombatState() == EBHEnemyCombatState::Attacking
					|| Enemy->GetCombatState() == EBHEnemyCombatState::Recovering);
			(bLocked ? LockedRequesters : OtherRequesters).Add(Requester);
		}
		Reservation.Reset();
	}

	auto AssignClosestFreeAttackSlot = [this](AActor* Requester)
	{
		if (!Requester)
		{
			return false;
		}

		int32 BestSlotIndex = INDEX_NONE;
		float BestDistanceSquared = TNumericLimits<float>::Max();
		for (int32 SlotIndex = 0; SlotIndex < GetActiveAttackSlotCount(); ++SlotIndex)
		{
			if (AttackReservations[SlotIndex].IsValid())
			{
				continue;
			}

			FVector SlotLocation;
			if (!GetPocketSlotWorldLocation(
				EBHCombatSlotType::Attack,
				SlotIndex,
				SlotLocation))
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared2D(
				Requester->GetActorLocation(),
				SlotLocation);
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				BestSlotIndex = SlotIndex;
			}
		}

		if (!AttackReservations.IsValidIndex(BestSlotIndex))
		{
			return false;
		}
		AttackReservations[BestSlotIndex] = Requester;
		return true;
	};

	for (const TWeakObjectPtr<AActor>& RequesterPtr : LockedRequesters)
	{
		if (!AssignClosestFreeAttackSlot(RequesterPtr.Get()))
		{
			AttackReservations = PreviousAttackReservations;
			return false;
		}
	}

	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<8>> OverflowRequesters;
	for (const TWeakObjectPtr<AActor>& RequesterPtr : OtherRequesters)
	{
		AActor* Requester = RequesterPtr.Get();
		if (Requester && !AssignClosestFreeAttackSlot(Requester))
		{
			OverflowRequesters.Add(Requester);
		}
	}

	for (const TWeakObjectPtr<AActor>& RequesterPtr : OverflowRequesters)
	{
		AActor* Requester = RequesterPtr.Get();
		if (!Requester)
		{
			continue;
		}
		const bool bDemotedToReservedLayer = TryReserveSlot(Requester, EBHCombatSlotType::Wait)
			|| TryReserveSlot(Requester, EBHCombatSlotType::Holding);
		if (!bDemotedToReservedLayer)
		{
			// The central queue remains valid, so this requester becomes Pending.
			NotifyRequesterSlotChanged(Requester);
		}
	}
	return true;
}

void UCombatEngagementSlotComponent::RepackCorridorLayerReservations(
	TArray<TWeakObjectPtr<AActor>>& Reservations,
	EBHCombatSlotType SlotType,
	bool bNotifyMovedRequesters)
{
	if (!IsCorridorFormationActive())
	{
		return;
	}

	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<16>> Requesters;
	for (TWeakObjectPtr<AActor>& Reservation : Reservations)
	{
		if (Reservation.IsValid())
		{
			Requesters.Add(Reservation);
		}
		Reservation.Reset();
	}
	Requesters.Sort([this](const TWeakObjectPtr<AActor>& Left, const TWeakObjectPtr<AActor>& Right)
	{
		return GetQueueSequence(Left.Get()) < GetQueueSequence(Right.Get());
	});

	const int32 ChannelCount = GetCorridorQueueChannelCount();
	TArray<int32, TInlineAllocator<8>> NextRowByChannel;
	NextRowByChannel.SetNumZeroed(ChannelCount);
	for (const TWeakObjectPtr<AActor>& RequesterPtr : Requesters)
	{
		AActor* Requester = RequesterPtr.Get();
		if (!Requester)
		{
			continue;
		}

		const int32 ChannelIndex = GetCorridorQueueChannelIndex(Requester);
		if (!NextRowByChannel.IsValidIndex(ChannelIndex))
		{
			continue;
		}

		const int32 SlotIndex = NextRowByChannel[ChannelIndex] * ChannelCount + ChannelIndex;
		if (!Reservations.IsValidIndex(SlotIndex))
		{
			if (SlotType == EBHCombatSlotType::Wait)
			{
				TryReserveSlot(Requester, EBHCombatSlotType::Holding);
			}
			if (bNotifyMovedRequesters)
			{
				NotifyRequesterSlotChanged(Requester);
			}
			continue;
		}

		Reservations[SlotIndex] = Requester;
		++NextRowByChannel[ChannelIndex];
		if (bNotifyMovedRequesters)
		{
			NotifyRequesterSlotChanged(Requester);
		}
	}

	UE_LOG(
		LogProjectBH,
		VeryVerbose,
		TEXT("%s repacked Corridor %s reservations without changing side/lane order."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		*StaticEnum<EBHCombatSlotType>()->GetNameStringByValue(static_cast<int64>(SlotType)));
}

void UCombatEngagementSlotComponent::RepackAllCorridorQueueLayers(bool bNotifyMovedRequesters)
{
	if (!IsCorridorFormationActive())
	{
		return;
	}

	RepackCorridorLayerReservations(
		WaitReservations,
		EBHCombatSlotType::Wait,
		bNotifyMovedRequesters);
	RepackCorridorLayerReservations(
		HoldingReservations,
		EBHCombatSlotType::Holding,
		bNotifyMovedRequesters);
}

void UCombatEngagementSlotComponent::AssignCorridorSideIndices(bool bResetExistingAssignments)
{
	if (!IsCorridorFormationActive())
	{
		return;
	}

	const AActor* Owner = GetOwner();
	const FVector AxisDirection = CorridorFormationRearDirection.GetSafeNormal2D();
	if (!Owner || AxisDirection.IsNearlyZero())
	{
		return;
	}

	int32 SideCounts[2] = { 0, 0 };
	for (FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (bResetExistingAssignments)
		{
			Entry.CorridorSideIndex = INDEX_NONE;
		}
		else if (FMath::IsWithin(Entry.CorridorSideIndex, 0, 2))
		{
			++SideCounts[Entry.CorridorSideIndex];
		}
	}

	for (FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* Requester = Entry.Requester.Get();
		if (!Requester || FMath::IsWithin(Entry.CorridorSideIndex, 0, 2))
		{
			continue;
		}

		const float AxisOffset = FVector::DotProduct(
			Requester->GetActorLocation() - Owner->GetActorLocation(),
			AxisDirection);
		if (FMath::Abs(AxisOffset) > 25.0f)
		{
			Entry.CorridorSideIndex = AxisOffset >= 0.0f ? 0 : 1;
		}
		else
		{
			Entry.CorridorSideIndex = SideCounts[0] <= SideCounts[1] ? 0 : 1;
		}
		++SideCounts[Entry.CorridorSideIndex];
	}
}

int32 UCombatEngagementSlotComponent::GetCorridorLaneIndex(AActor* Requester) const
{
	if (!Requester)
	{
		return INDEX_NONE;
	}

	const int32 LaneCount = FMath::Max(1, ActiveCorridorLaneCount);
	const uint64 Sequence = GetQueueSequence(Requester);
	if (Sequence == TNumericLimits<uint64>::Max())
	{
		return static_cast<int32>(Requester->GetUniqueID() % static_cast<uint32>(LaneCount));
	}

	return static_cast<int32>((Sequence - 1) % static_cast<uint64>(LaneCount));
}

int32 UCombatEngagementSlotComponent::GetCorridorSideIndex(AActor* Requester) const
{
	if (!Requester)
	{
		return INDEX_NONE;
	}

	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (Entry.Requester.Get() == Requester
			&& FMath::IsWithin(Entry.CorridorSideIndex, 0, 2))
		{
			return Entry.CorridorSideIndex;
		}
	}

	const AActor* Owner = GetOwner();
	const FVector AxisDirection = CorridorFormationRearDirection.GetSafeNormal2D();
	if (!Owner || AxisDirection.IsNearlyZero())
	{
		return static_cast<int32>(Requester->GetUniqueID() % 2u);
	}

	return FVector::DotProduct(
		Requester->GetActorLocation() - Owner->GetActorLocation(),
		AxisDirection) >= 0.0f ? 0 : 1;
}

int32 UCombatEngagementSlotComponent::GetCorridorQueueChannelIndex(AActor* Requester) const
{
	const int32 LaneIndex = GetCorridorLaneIndex(Requester);
	const int32 SideIndex = GetCorridorSideIndex(Requester);
	if (LaneIndex == INDEX_NONE || SideIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	return SideIndex * FMath::Max(1, ActiveCorridorLaneCount) + LaneIndex;
}

int32 UCombatEngagementSlotComponent::GetCorridorQueueChannelCount() const
{
	return 2 * FMath::Max(1, ActiveCorridorLaneCount);
}

int32 UCombatEngagementSlotComponent::GetCorridorAttackSlotSideIndex(int32 AttackSlotIndex) const
{
	return AttackSlotIndex >= 0 ? AttackSlotIndex % 2 : INDEX_NONE;
}

int32 UCombatEngagementSlotComponent::GetCorridorAttackSlotChannelIndex(int32 AttackSlotIndex) const
{
	if (AttackSlotIndex < 0)
	{
		return INDEX_NONE;
	}

	const int32 LaneCount = FMath::Max(1, ActiveCorridorLaneCount);
	const int32 SideIndex = GetCorridorAttackSlotSideIndex(AttackSlotIndex);
	const int32 SideLocalIndex = AttackSlotIndex / 2;
	return SideIndex * LaneCount + SideLocalIndex % LaneCount;
}

bool UCombatEngagementSlotComponent::HasFreeCorridorLaneSlot(
	const TArray<TWeakObjectPtr<AActor>>& Reservations,
	int32 ChannelIndex) const
{
	const int32 ChannelCount = GetCorridorQueueChannelCount();
	if (!FMath::IsWithin(ChannelIndex, 0, ChannelCount))
	{
		return false;
	}

	for (int32 SlotIndex = ChannelIndex; SlotIndex < Reservations.Num(); SlotIndex += ChannelCount)
	{
		if (!Reservations[SlotIndex].IsValid())
		{
			return true;
		}
	}
	return false;
}

bool UCombatEngagementSlotComponent::FindCorridorLayerHead(
	const TArray<TWeakObjectPtr<AActor>>& Reservations,
	EBHCombatSlotType SlotType,
	int32 ChannelIndex,
	bool bRequireArrival,
	int32& OutSlotIndex) const
{
	OutSlotIndex = INDEX_NONE;
	const int32 ChannelCount = GetCorridorQueueChannelCount();
	if (!FMath::IsWithin(ChannelIndex, 0, ChannelCount))
	{
		return false;
	}

	for (int32 SlotIndex = ChannelIndex; SlotIndex < Reservations.Num(); SlotIndex += ChannelCount)
	{
		AActor* Requester = Reservations[SlotIndex].Get();
		if (!Requester)
		{
			continue;
		}

		if (bRequireArrival)
		{
			FVector SlotLocation;
			if (!GetSlotWorldLocation(SlotType, SlotIndex, SlotLocation)
				|| FVector::DistSquared2D(Requester->GetActorLocation(), SlotLocation)
					> FMath::Square(PromotionArrivalRadius))
			{
				return false;
			}
		}

		OutSlotIndex = SlotIndex;
		return true;
	}
	return false;
}

bool UCombatEngagementSlotComponent::FindCorridorPromotionCandidate(
	const TArray<TWeakObjectPtr<AActor>>& SourceReservations,
	EBHCombatSlotType SourceType,
	const TArray<TWeakObjectPtr<AActor>>& DestinationReservations,
	int32& OutSourceIndex) const
{
	OutSourceIndex = INDEX_NONE;
	uint64 BestSequence = TNumericLimits<uint64>::Max();
	for (int32 ChannelIndex = 0; ChannelIndex < GetCorridorQueueChannelCount(); ++ChannelIndex)
	{
		if (!HasFreeCorridorLaneSlot(DestinationReservations, ChannelIndex))
		{
			continue;
		}

		int32 HeadIndex = INDEX_NONE;
		if (!FindCorridorLayerHead(SourceReservations, SourceType, ChannelIndex, true, HeadIndex))
		{
			continue;
		}

		AActor* Requester = SourceReservations[HeadIndex].Get();
		const uint64 Sequence = GetQueueSequence(Requester);
		if (OutSourceIndex == INDEX_NONE || Sequence < BestSequence)
		{
			OutSourceIndex = HeadIndex;
			BestSequence = Sequence;
		}
	}
	return OutSourceIndex != INDEX_NONE;
}

bool UCombatEngagementSlotComponent::FindCorridorPendingCandidateForHolding(AActor*& OutRequester) const
{
	OutRequester = nullptr;
	uint64 BestSequence = TNumericLimits<uint64>::Max();
	for (int32 ChannelIndex = 0; ChannelIndex < GetCorridorQueueChannelCount(); ++ChannelIndex)
	{
		if (!HasFreeCorridorLaneSlot(HoldingReservations, ChannelIndex))
		{
			continue;
		}

		AActor* ChannelHead = nullptr;
		uint64 ChannelHeadSequence = TNumericLimits<uint64>::Max();
		for (const FEngagementQueueEntry& Entry : EngagementQueue)
		{
			AActor* Requester = Entry.Requester.Get();
			if (!Requester || IsRequesterReserved(Requester)
				|| GetCorridorQueueChannelIndex(Requester) != ChannelIndex
				|| Entry.Sequence >= ChannelHeadSequence)
			{
				continue;
			}
			ChannelHead = Requester;
			ChannelHeadSequence = Entry.Sequence;
		}

		if (ChannelHead && (!OutRequester || ChannelHeadSequence < BestSequence))
		{
			OutRequester = ChannelHead;
			BestSequence = ChannelHeadSequence;
		}
	}
	return OutRequester != nullptr;
}

bool UCombatEngagementSlotComponent::FindCorridorWaitAdmissionForAttackSlot(
	int32 AttackSlotIndex,
	bool bAllowOtherLane,
	int32& OutWaitSlotIndex) const
{
	OutWaitSlotIndex = INDEX_NONE;
	if (!IsCorridorFormationActive()
		|| !AttackReservations.IsValidIndex(AttackSlotIndex)
		|| AttackSlotIndex >= GetActiveAttackSlotCount())
	{
		return false;
	}

	FVector AttackSlotLocation;
	if (!GetSlotWorldLocation(EBHCombatSlotType::Attack, AttackSlotIndex, AttackSlotLocation))
	{
		return false;
	}

	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	auto IsReadyHead = [this, CurrentTime, &AttackSlotLocation](int32 ChannelIndex, int32& OutHeadIndex)
	{
		OutHeadIndex = INDEX_NONE;
		if (!FindCorridorLayerHead(
			WaitReservations,
			EBHCombatSlotType::Wait,
			ChannelIndex,
			true,
			OutHeadIndex))
		{
			return false;
		}

		AActor* Requester = WaitReservations[OutHeadIndex].Get();
		if (!Requester || CurrentTime < GetAttackEligibleTime(Requester))
		{
			return false;
		}

		const float MaximumAttackSlotDistance = GetMaximumAttackSlotDistance(Requester);
		if (FVector::DistSquared2D(GetOwner()->GetActorLocation(), AttackSlotLocation)
			> FMath::Square(MaximumAttackSlotDistance))
		{
			return false;
		}

		float PathScore = 0.0f;
		return GetNavigationPathScore(Requester, AttackSlotLocation, PathScore);
	};

	const int32 TargetChannelIndex = GetCorridorAttackSlotChannelIndex(AttackSlotIndex);
	if (IsReadyHead(TargetChannelIndex, OutWaitSlotIndex))
	{
		return true;
	}
	if (!bAllowOtherLane)
	{
		return false;
	}

	uint64 BestSequence = TNumericLimits<uint64>::Max();
	const int32 LaneCount = FMath::Max(1, ActiveCorridorLaneCount);
	const int32 TargetSideIndex = GetCorridorAttackSlotSideIndex(AttackSlotIndex);
	for (int32 ChannelIndex = 0; ChannelIndex < GetCorridorQueueChannelCount(); ++ChannelIndex)
	{
		if (ChannelIndex == TargetChannelIndex || ChannelIndex / LaneCount != TargetSideIndex)
		{
			continue;
		}

		int32 HeadIndex = INDEX_NONE;
		if (!IsReadyHead(ChannelIndex, HeadIndex))
		{
			continue;
		}

		const uint64 Sequence = GetQueueSequence(WaitReservations[HeadIndex].Get());
		if (OutWaitSlotIndex == INDEX_NONE || Sequence < BestSequence)
		{
			OutWaitSlotIndex = HeadIndex;
			BestSequence = Sequence;
		}
	}
	return OutWaitSlotIndex != INDEX_NONE;
}

int32 UCombatEngagementSlotComponent::GetLockedAttackReservationCount() const
{
	int32 LockedCount = 0;
	for (const TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		const ABHEnemy* Enemy = Cast<ABHEnemy>(Reservation.Get());
		if (Enemy
			&& (Enemy->GetCombatState() == EBHEnemyCombatState::Attacking
				|| Enemy->GetCombatState() == EBHEnemyCombatState::Recovering))
		{
			++LockedCount;
		}
	}
	return LockedCount;
}

FVector UCombatEngagementSlotComponent::ResolveCorridorRearDirection(const FVector& UnsignedAxis) const
{
	FVector Axis = UnsignedAxis.GetSafeNormal2D();
	const AActor* Owner = GetOwner();
	if (!Owner || Axis.IsNearlyZero())
	{
		return -FVector::ForwardVector;
	}

	float CrowdSideScore = 0.0f;
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		const AActor* Requester = Entry.Requester.Get();
		if (!Requester)
		{
			continue;
		}

		CrowdSideScore += FVector::DotProduct(
			Requester->GetActorLocation() - Owner->GetActorLocation(),
			Axis);
	}
	if (FMath::Abs(CrowdSideScore) > 25.0f)
	{
		return CrowdSideScore >= 0.0f ? Axis : -Axis;
	}

	const float AxisVelocity = FVector::DotProduct(Owner->GetVelocity(), Axis);
	if (FMath::Abs(AxisVelocity) > 10.0f)
	{
		return AxisVelocity >= 0.0f ? -Axis : Axis;
	}

	return FVector::DotProduct(Owner->GetActorForwardVector(), Axis) >= 0.0f
		? -Axis
		: Axis;
}

bool UCombatEngagementSlotComponent::IsCorridorFormationActive() const
{
	return bEnableCorridorFormation
		&& bHasValidSpaceAnalysis
		&& CurrentSpaceMode == EBHCombatSpaceMode::Corridor;
}

bool UCombatEngagementSlotComponent::IsPocketFormationActive() const
{
	return bHasValidSpaceAnalysis
		&& CurrentSpaceMode == EBHCombatSpaceMode::Pocket;
}

int32 UCombatEngagementSlotComponent::CalculateCorridorLaneCount(float CorridorWidth) const
{
	const float AgentRadius = FMath::Max(1.0f, CorridorAgentRadius);
	const float Spacing = FMath::Max(1.0f, CorridorSlotSpacing);
	const float UsableCenterWidth = FMath::Max(0.0f, CorridorWidth - 2.0f * AgentRadius);
	const int32 WidthLimitedLaneCount = 1 + FMath::FloorToInt(UsableCenterWidth / Spacing);
	return FMath::Clamp(WidthLimitedLaneCount, 1, FMath::Clamp(CorridorMaximumLaneCount, 1, 4));
}

int32 UCombatEngagementSlotComponent::CalculateCorridorAttackSlotCount(float CorridorWidth) const
{
	const int32 MaximumAttackCount = FMath::Max(1, AttackReservations.Num());
	const float AttackRadius = FMath::Max(1.0f, AttackRingRadius);
	const float AgentRadius = FMath::Max(1.0f, CorridorAgentRadius);
	const float Spacing = FMath::Min(FMath::Max(1.0f, CorridorSlotSpacing), 2.0f * AttackRadius);
	const float StepRadians = 2.0f * FMath::Asin(FMath::Clamp(Spacing / (2.0f * AttackRadius), 0.0f, 1.0f));
	const float MaximumHalfAngleRadians = FMath::DegreesToRadians(
		FMath::Clamp(CorridorAttackArcHalfAngle, 0.0f, 89.0f));

	int32 ResolvedAttackCount = 1;
	for (int32 CandidateCount = 2; CandidateCount <= MaximumAttackCount; ++CandidateCount)
	{
		const float HalfSpanRadians = 0.5f * static_cast<float>(CandidateCount - 1) * StepRadians;
		if (HalfSpanRadians > MaximumHalfAngleRadians)
		{
			break;
		}

		const float RequiredWidth = 2.0f * AgentRadius
			+ 2.0f * AttackRadius * FMath::Sin(HalfSpanRadians);
		if (RequiredWidth > CorridorWidth)
		{
			break;
		}
		ResolvedAttackCount = CandidateCount;
	}

	return ResolvedAttackCount;
}

int32 UCombatEngagementSlotComponent::CalculatePocketAttackSlotCount() const
{
	const int32 MaximumAttackCount = FMath::Max(1, AttackReservations.Num());
	const float Radius = FMath::Max(1.0f, AttackRingRadius);
	const float ArcHalfAngle = FMath::Clamp(
		FMath::Min(PocketMaximumArcHalfAngle, 0.5f * EstimatedPocketOpenArc),
		10.0f,
		170.0f);
	const float Spacing = FMath::Min(FMath::Max(1.0f, PocketSlotSpacing), 2.0f * Radius);
	const float StepAngle = FMath::RadiansToDegrees(2.0f * FMath::Asin(FMath::Clamp(
		Spacing / (2.0f * Radius),
		0.0f,
		1.0f)));
	const int32 ArcCapacity = 1 + FMath::FloorToInt(
		(2.0f * ArcHalfAngle) / FMath::Max(1.0f, StepAngle));
	return FMath::Clamp(ArcCapacity, 1, MaximumAttackCount);
}

float UCombatEngagementSlotComponent::GetCorridorLayerStartDistance(EBHCombatSlotType SlotType) const
{
	const int32 ChannelCount = GetCorridorQueueChannelCount();
	const float RowSpacing = FMath::Max(1.0f, CorridorRowSpacing);
	const float LayerGap = FMath::Max(0.0f, CorridorLayerGap);
	const float WaitStart = FMath::Max(0.0f, AttackRingRadius) + LayerGap;
	if (SlotType == EBHCombatSlotType::Wait)
	{
		return WaitStart;
	}

	const int32 WaitRowCount = FMath::Max(1, FMath::DivideAndRoundUp(WaitReservations.Num(), ChannelCount));
	const float HoldingStart = WaitStart
		+ static_cast<float>(WaitRowCount - 1) * RowSpacing
		+ LayerGap;
	if (SlotType == EBHCombatSlotType::Holding)
	{
		return HoldingStart;
	}

	const int32 HoldingRowCount = FMath::Max(1, FMath::DivideAndRoundUp(HoldingReservations.Num(), ChannelCount));
	return HoldingStart
		+ static_cast<float>(HoldingRowCount - 1) * RowSpacing
		+ LayerGap;
}

bool UCombatEngagementSlotComponent::GetCorridorSlotWorldLocation(
	EBHCombatSlotType SlotType,
	int32 SlotIndex,
	FVector& OutWorldLocation) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || SlotIndex < 0 || !IsCorridorFormationActive())
	{
		return false;
	}

	const FVector AxisDirection = CorridorFormationRearDirection.GetSafeNormal2D();
	if (AxisDirection.IsNearlyZero())
	{
		return false;
	}

	if (SlotType == EBHCombatSlotType::Attack)
	{
		const int32 ActiveAttackCount = GetActiveAttackSlotCount();
		if (SlotIndex >= ActiveAttackCount)
		{
			return false;
		}

		const float AttackRadius = FMath::Max(1.0f, AttackRingRadius);
		const float Spacing = FMath::Min(FMath::Max(1.0f, CorridorSlotSpacing), 2.0f * AttackRadius);
		const float StepRadians = 2.0f * FMath::Asin(FMath::Clamp(
			Spacing / (2.0f * AttackRadius),
			0.0f,
			1.0f));
		const int32 SideIndex = GetCorridorAttackSlotSideIndex(SlotIndex);
		const int32 SideLocalIndex = SlotIndex / 2;
		const int32 SideAttackCount = SideIndex == 0
			? FMath::DivideAndRoundUp(ActiveAttackCount, 2)
			: ActiveAttackCount / 2;
		const float CenteredIndex = static_cast<float>(SideLocalIndex)
			- 0.5f * static_cast<float>(SideAttackCount - 1);
		const FVector SideDirection = SideIndex == 0 ? AxisDirection : -AxisDirection;
		const FVector AttackDirection = SideDirection.RotateAngleAxis(
			FMath::RadiansToDegrees(CenteredIndex * StepRadians),
			FVector::UpVector);
		return ProjectToNavigation(
			Owner->GetActorLocation() + AttackDirection * AttackRadius,
			OutWorldLocation);
	}

	const int32 LaneCount = FMath::Max(1, ActiveCorridorLaneCount);
	const int32 ChannelCount = GetCorridorQueueChannelCount();
	const int32 ChannelIndex = SlotIndex % ChannelCount;
	const int32 SideIndex = ChannelIndex / LaneCount;
	const int32 LaneIndex = ChannelIndex % LaneCount;
	const int32 RowIndex = SlotIndex / ChannelCount;
	const float LateralOffset = (static_cast<float>(LaneIndex)
		- 0.5f * static_cast<float>(LaneCount - 1))
		* FMath::Max(1.0f, CorridorSlotSpacing);
	const FVector SideDirection = SideIndex == 0 ? AxisDirection : -AxisDirection;
	const FVector RightDirection(-AxisDirection.Y, AxisDirection.X, 0.0f);
	const float AnchorLag = FMath::Max(0.0f, FVector::DotProduct(
		EngagementAnchorLocation - Owner->GetActorLocation(),
		SideDirection));
	const FVector OuterCenter = Owner->GetActorLocation() + SideDirection * AnchorLag;
	const float LongitudinalDistance = GetCorridorLayerStartDistance(SlotType)
		+ static_cast<float>(RowIndex) * FMath::Max(1.0f, CorridorRowSpacing);
	return ProjectToNavigation(
		OuterCenter + SideDirection * LongitudinalDistance + RightDirection * LateralOffset,
		OutWorldLocation);
}

bool UCombatEngagementSlotComponent::GetCorridorPendingWorldLocation(
	int32 PendingIndex,
	FVector& OutWorldLocation) const
{
	if (PendingIndex < 0)
	{
		return false;
	}

	return GetCorridorSlotWorldLocation(
		EBHCombatSlotType::Pending,
		PendingIndex,
		OutWorldLocation);
}

bool UCombatEngagementSlotComponent::GetPocketSlotWorldLocation(
	EBHCombatSlotType SlotType,
	int32 SlotIndex,
	FVector& OutWorldLocation) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || SlotIndex < 0 || !IsPocketFormationActive())
	{
		return false;
	}

	int32 SlotCount = 0;
	float BaseRadius = 0.0f;
	FVector Center = EngagementAnchorLocation;
	switch (SlotType)
	{
	case EBHCombatSlotType::Attack:
		SlotCount = GetActiveAttackSlotCount();
		BaseRadius = AttackRingRadius;
		Center = Owner->GetActorLocation();
		break;
	case EBHCombatSlotType::Wait:
		SlotCount = WaitReservations.Num();
		BaseRadius = WaitRingRadius;
		break;
	case EBHCombatSlotType::Holding:
		SlotCount = HoldingReservations.Num();
		BaseRadius = HoldingRingRadius;
		break;
	default:
		return false;
	}

	return SlotIndex < SlotCount
		&& GetPocketFanWorldLocation(
			Center,
			BaseRadius,
			SlotIndex,
			SlotCount,
			SlotType != EBHCombatSlotType::Attack,
			OutWorldLocation);
}

bool UCombatEngagementSlotComponent::GetPocketPendingWorldLocation(
	int32 PendingIndex,
	FVector& OutWorldLocation) const
{
	if (PendingIndex < 0 || !IsPocketFormationActive())
	{
		return false;
	}

	const int32 StablePendingCount = FMath::Max(
		FMath::Max(1, PendingSlotsPerRing),
		PendingIndex + 1);
	return GetPocketFanWorldLocation(
		EngagementAnchorLocation,
		PendingRingRadius,
		PendingIndex,
		StablePendingCount,
		true,
		OutWorldLocation);
}

bool UCombatEngagementSlotComponent::GetPocketFanWorldLocation(
	const FVector& Center,
	float BaseRadius,
	int32 SlotIndex,
	int32 SlotCount,
	bool bAllowMultipleRows,
	FVector& OutWorldLocation) const
{
	if (SlotIndex < 0 || SlotCount <= 0 || SlotIndex >= SlotCount)
	{
		return false;
	}

	FVector OpenDirection = PocketFormationDirection.GetSafeNormal2D();
	if (OpenDirection.IsNearlyZero())
	{
		OpenDirection = EstimatedPocketOpenDirection.GetSafeNormal2D();
	}
	if (OpenDirection.IsNearlyZero())
	{
		return false;
	}

	const float Radius = FMath::Max(1.0f, BaseRadius);
	const float ArcHalfAngle = FMath::Clamp(
		FMath::Min(PocketMaximumArcHalfAngle, 0.5f * EstimatedPocketOpenArc),
		10.0f,
		170.0f);
	const float Spacing = FMath::Min(FMath::Max(1.0f, PocketSlotSpacing), 2.0f * Radius);
	const float BaseStepAngle = FMath::RadiansToDegrees(2.0f * FMath::Asin(FMath::Clamp(
		Spacing / (2.0f * Radius),
		0.0f,
		1.0f)));
	const int32 SlotsPerRow = bAllowMultipleRows
		? FMath::Max(
			1,
			FMath::Min(
				SlotCount,
				1 + FMath::FloorToInt((2.0f * ArcHalfAngle) / FMath::Max(1.0f, BaseStepAngle))))
		: SlotCount;
	const int32 RowIndex = SlotIndex / SlotsPerRow;
	const int32 SlotIndexOnRow = SlotIndex % SlotsPerRow;
	const int32 RowStartIndex = RowIndex * SlotsPerRow;
	const int32 SlotsOnRow = FMath::Min(SlotsPerRow, SlotCount - RowStartIndex);
	const float RowRadius = Radius + static_cast<float>(RowIndex) * FMath::Max(1.0f, PocketRowSpacing);
	const float RowSpacing = FMath::Min(FMath::Max(1.0f, PocketSlotSpacing), 2.0f * RowRadius);
	const float RowStepAngle = FMath::RadiansToDegrees(2.0f * FMath::Asin(FMath::Clamp(
		RowSpacing / (2.0f * RowRadius),
		0.0f,
		1.0f)));
	const float CenteredIndex = static_cast<float>(SlotIndexOnRow)
		- 0.5f * static_cast<float>(SlotsOnRow - 1);
	const float FittedStepAngle = SlotsOnRow > 1
		? FMath::Min(RowStepAngle, (2.0f * ArcHalfAngle) / static_cast<float>(SlotsOnRow - 1))
		: 0.0f;
	const float DesiredAngle = FMath::Clamp(
		CenteredIndex * FittedStepAngle,
		-ArcHalfAngle,
		ArcHalfAngle);

	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	for (int32 AttemptIndex = 0; AttemptIndex < 7; ++AttemptIndex)
	{
		const float AngleScale = 1.0f - static_cast<float>(AttemptIndex) / 7.0f;
		const FVector SlotDirection = OpenDirection.RotateAngleAxis(
			DesiredAngle * AngleScale,
			FVector::UpVector);
		const FVector DesiredLocation = Center + SlotDirection * RowRadius;
		FVector HitLocation = DesiredLocation;
		const bool bPathBlocked = NavigationSystem
			&& NavigationSystem->NavigationRaycast(World, Center, DesiredLocation, HitLocation);
		if (!bPathBlocked && ProjectToNavigation(DesiredLocation, OutWorldLocation))
		{
			return true;
		}
	}

	return ProjectToNavigation(Center + OpenDirection * RowRadius, OutWorldLocation);
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
	if (IsCorridorFormationActive())
	{
		return GetCorridorPendingWorldLocation(OutPendingIndex, OutWorldLocation);
	}
	if (IsPocketFormationActive())
	{
		return GetPocketPendingWorldLocation(OutPendingIndex, OutWorldLocation);
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
	if (IsCorridorFormationActive() || IsPocketFormationActive())
	{
		// Constrained-space slots already face their valid approach area. Full-ring
		// orbiting would push actors into walls and recreate the bottleneck.
		return true;
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

	bool bReleasedReservation = false;
	for (TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
			bReleasedReservation = true;
		}
	}

	for (TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
			bReleasedReservation = true;
		}
	}

	for (TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
			bReleasedReservation = true;
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
	if (bReleasedReservation && IsCorridorFormationActive())
	{
		RepackAllCorridorQueueLayers(true);
		++FormationRevision;
		NotifyAllReservedRequestersSlotChanged();
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
	if (IsCorridorFormationActive())
	{
		RepackCorridorLayerReservations(
			WaitReservations,
			EBHCombatSlotType::Wait,
			true);
		NotifyRequesterSlotChanged(PromotedRequester);
		NotifyRequesterSlotChanged(Requester);
	}

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
	bool bPrunedReservation = false;
	for (TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		if (!Reservation.IsValid())
		{
			bPrunedReservation |= Reservation.IsStale();
			Reservation.Reset();
		}
	}

	for (TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		if (!Reservation.IsValid())
		{
			bPrunedReservation |= Reservation.IsStale();
			Reservation.Reset();
		}
	}

	for (TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		if (!Reservation.IsValid())
		{
			bPrunedReservation |= Reservation.IsStale();
			Reservation.Reset();
		}
	}

	for (int32 QueueIndex = EngagementQueue.Num() - 1; QueueIndex >= 0; --QueueIndex)
	{
		if (!EngagementQueue[QueueIndex].Requester.IsValid())
		{
			bPrunedReservation = true;
			EngagementQueue.RemoveAtSwap(QueueIndex, 1, EAllowShrinking::No);
		}
	}

	if (EngagementQueue.IsEmpty())
	{
		bInitialFormationActive = true;
		LastRequesterRegistrationTime = 0.0f;
	}
	if (bPrunedReservation && IsCorridorFormationActive())
	{
		RepackAllCorridorQueueLayers(true);
		++FormationRevision;
		NotifyAllReservedRequestersSlotChanged();
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
	NotifyAllReservedRequestersSlotChanged();
}

void UCombatEngagementSlotComponent::NotifyRequesterSlotChanged(AActor* Requester) const
{
	const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
	if (ABHCrowdEnemyAIController* Controller = Enemy
		? Cast<ABHCrowdEnemyAIController>(Enemy->GetController())
		: nullptr)
	{
		Controller->NotifyCombatSlotAssignmentChanged();
	}
}

void UCombatEngagementSlotComponent::NotifyAllReservedRequestersSlotChanged() const
{
	for (const TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		NotifyRequesterSlotChanged(Reservation.Get());
	}
	for (const TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		NotifyRequesterSlotChanged(Reservation.Get());
	}
	for (const TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		NotifyRequesterSlotChanged(Reservation.Get());
	}
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
	if (IsCorridorFormationActive())
	{
		RepackCorridorLayerReservations(
			WaitReservations,
			EBHCombatSlotType::Wait,
			true);
	}
	NotifyRequesterSlotChanged(Requester);
	const TCHAR* AdmissionRule = IsCorridorFormationActive()
		? TEXT("lane-head queue rule")
		: TEXT("congestion-aware path cost");
	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s admitted %s from Wait to Attack slot %d by %s."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		*Requester->GetName(),
		AttackSlotIndex,
		AdmissionRule);
	return true;
}

bool UCombatEngagementSlotComponent::PromoteOldestReservation(
	TArray<TWeakObjectPtr<AActor>>& SourceReservations,
	EBHCombatSlotType SourceType,
	EBHCombatSlotType DestinationType)
{
	int32 SourceIndex = INDEX_NONE;
	const bool bFoundSource = IsCorridorFormationActive()
		? FindCorridorPromotionCandidate(
			SourceReservations,
			SourceType,
			DestinationType == EBHCombatSlotType::Wait ? WaitReservations : AttackReservations,
			SourceIndex)
		: FindOldestEligibleReservation(SourceReservations, SourceType, SourceIndex);
	if (!bFoundSource)
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
	if (IsCorridorFormationActive())
	{
		RepackCorridorLayerReservations(SourceReservations, SourceType, true);
		if (DestinationType == EBHCombatSlotType::Wait)
		{
			RepackCorridorLayerReservations(
				WaitReservations,
				EBHCombatSlotType::Wait,
				true);
		}
	}
	NotifyRequesterSlotChanged(Requester);

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
	const bool bFoundRequester = IsCorridorFormationActive()
		? FindCorridorPendingCandidateForHolding(Requester)
		: FindOldestPendingRequester(Requester);
	if (!bFoundRequester)
	{
		return false;
	}

	if (!TryReserveSlot(Requester, EBHCombatSlotType::Holding))
	{
		return false;
	}

	NotifyRequesterSlotChanged(Requester);
	return true;
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
	if (IsCorridorFormationActive())
	{
		for (int32 PassIndex = 0; PassIndex < 2; ++PassIndex)
		{
			const bool bAllowOtherLane = PassIndex > 0;
			uint64 BestSequence = TNumericLimits<uint64>::Max();
			for (int32 AttackSlotIndex = 0;
				AttackSlotIndex < GetActiveAttackSlotCount();
				++AttackSlotIndex)
			{
				if (AttackReservations[AttackSlotIndex].IsValid())
				{
					continue;
				}

				int32 WaitSlotIndex = INDEX_NONE;
				if (!FindCorridorWaitAdmissionForAttackSlot(
					AttackSlotIndex,
					bAllowOtherLane,
					WaitSlotIndex))
				{
					continue;
				}

				const uint64 Sequence = GetQueueSequence(WaitReservations[WaitSlotIndex].Get());
				if (OutWaitSlotIndex == INDEX_NONE || Sequence < BestSequence)
				{
					OutWaitSlotIndex = WaitSlotIndex;
					OutAttackSlotIndex = AttackSlotIndex;
					BestSequence = Sequence;
				}
			}

			if (OutWaitSlotIndex != INDEX_NONE)
			{
				return true;
			}
		}
		return false;
	}

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
	if (IsCorridorFormationActive())
	{
		return FindCorridorWaitAdmissionForAttackSlot(
			AttackSlotIndex,
			true,
			OutWaitSlotIndex);
	}
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
			if (IsCorridorFormationActive()
				&& GetCorridorQueueChannelIndex(Requester)
					!= GetCorridorAttackSlotChannelIndex(AttackSlotIndex))
			{
				continue;
			}
			if (IsCorridorFormationActive())
			{
				bool bHasOlderChannelHead = false;
				const int32 RequesterChannel = GetCorridorQueueChannelIndex(Requester);
				for (const FEngagementQueueEntry& OtherEntry : EngagementQueue)
				{
					AActor* OtherRequester = OtherEntry.Requester.Get();
					if (OtherRequester
						&& OtherEntry.Sequence < Entry.Sequence
						&& !IsRequesterReserved(OtherRequester)
						&& GetCorridorQueueChannelIndex(OtherRequester) == RequesterChannel)
					{
						bHasOlderChannelHead = true;
						break;
					}
				}
				if (bHasOlderChannelHead)
				{
					continue;
				}
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

	const bool bCorridorQueue = IsCorridorFormationActive();
	const int32 ChannelCount = GetCorridorQueueChannelCount();
	const int32 RequesterChannelIndex = bCorridorQueue
		? GetCorridorQueueChannelIndex(Requester)
		: INDEX_NONE;
	int32 PendingBeforeRequester = 0;
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* OtherRequester = Entry.Requester.Get();
		if (!OtherRequester || IsRequesterReserved(OtherRequester))
		{
			continue;
		}

		if (Entry.Sequence < RequesterSequence
			&& (!bCorridorQueue
				|| GetCorridorQueueChannelIndex(OtherRequester) == RequesterChannelIndex))
		{
			++PendingBeforeRequester;
		}
	}

	OutPendingIndex = bCorridorQueue
		? PendingBeforeRequester * ChannelCount + RequesterChannelIndex
		: PendingBeforeRequester;
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
	if (IsCorridorFormationActive())
	{
		AssignCorridorSideIndices(false);
	}
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
	if (IsCorridorFormationActive())
	{
		ReconcileCorridorAttackReservations();
		RepackAllCorridorQueueLayers(true);
		++FormationRevision;
		NotifyAllReservedRequestersSlotChanged();
		return;
	}

	ReformRingReservations(AttackReservations, EBHCombatSlotType::Attack);
	ReformRingReservations(WaitReservations, EBHCombatSlotType::Wait);
	ReformRingReservations(HoldingReservations, EBHCombatSlotType::Holding);
	++FormationRevision;
	NotifyAllReservedRequestersSlotChanged();

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
	const bool bUsesCorridorChannel = IsCorridorFormationActive();
	const int32 RequesterChannelIndex = bUsesCorridorChannel
		? GetCorridorQueueChannelIndex(Requester)
		: INDEX_NONE;
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
		const int32 SlotChannelIndex = SlotType == EBHCombatSlotType::Attack
			? GetCorridorAttackSlotChannelIndex(SlotIndex)
			: SlotIndex % GetCorridorQueueChannelCount();
		if (bUsesCorridorChannel && SlotChannelIndex != RequesterChannelIndex)
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
		if (bUsesCorridorChannel)
		{
			// Slot arrays are row-major. The first free index in this stable side/lane channel
			// is always the legal queue destination, regardless of Euclidean distance.
			BestSlotIndex = SlotIndex;
			break;
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
	if (IsCorridorFormationActive())
	{
		return GetCorridorSlotWorldLocation(SlotType, SlotIndex, OutWorldLocation);
	}
	if (IsPocketFormationActive())
	{
		return GetPocketSlotWorldLocation(SlotType, SlotIndex, OutWorldLocation);
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

	DrawCombatSpaceAnalysisDebug();

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

void UCombatEngagementSlotComponent::DrawCombatSpaceAnalysisDebug() const
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner || !bEnableCombatSpaceAnalysis)
	{
		return;
	}

	if (!bHasValidSpaceAnalysis || SpaceProbeEndpoints.Num() < 4)
	{
		DrawDebugString(
			World,
			Owner->GetActorLocation() + FVector(0.0f, 0.0f, 230.0f),
			TEXT("Space: Invalid NavMesh sample"),
			nullptr,
			FColor::Red,
			0.12f,
			false,
			1.0f);
		return;
	}

	const FVector DrawOrigin = SpaceProbeOrigin + FVector(0.0f, 0.0f, 8.0f);
	for (int32 ProbeIndex = 0; ProbeIndex < SpaceProbeEndpoints.Num(); ++ProbeIndex)
	{
		const bool bBlocked = SpaceProbeBlocked.IsValidIndex(ProbeIndex)
			&& SpaceProbeBlocked[ProbeIndex] != 0;
		DrawDebugLine(
			World,
			DrawOrigin,
			SpaceProbeEndpoints[ProbeIndex] + FVector(0.0f, 0.0f, 8.0f),
			bBlocked ? FColor(180, 70, 30) : FColor(70, 110, 70),
			false,
			0.12f,
			0,
			0.75f);
	}

	const int32 HalfProbeCount = SpaceProbeEndpoints.Num() / 2;
	const int32 AxisOppositeIndex = CorridorAxisProbeIndex + HalfProbeCount;
	const int32 WidthOppositeIndex = CorridorWidthProbeIndex + HalfProbeCount;
	if (SpaceProbeEndpoints.IsValidIndex(CorridorAxisProbeIndex)
		&& SpaceProbeEndpoints.IsValidIndex(AxisOppositeIndex))
	{
		DrawDebugLine(
			World,
			SpaceProbeEndpoints[CorridorAxisProbeIndex] + FVector(0.0f, 0.0f, 12.0f),
			SpaceProbeEndpoints[AxisOppositeIndex] + FVector(0.0f, 0.0f, 12.0f),
			FColor::Cyan,
			false,
			0.12f,
			0,
			4.0f);
	}
	if (SpaceProbeEndpoints.IsValidIndex(CorridorWidthProbeIndex)
		&& SpaceProbeEndpoints.IsValidIndex(WidthOppositeIndex))
	{
		DrawDebugLine(
			World,
			SpaceProbeEndpoints[CorridorWidthProbeIndex] + FVector(0.0f, 0.0f, 14.0f),
			SpaceProbeEndpoints[WidthOppositeIndex] + FVector(0.0f, 0.0f, 14.0f),
			FColor::Yellow,
			false,
			0.12f,
			0,
			4.0f);
	}
	if (IsCorridorFormationActive())
	{
		DrawDebugDirectionalArrow(
			World,
			DrawOrigin,
			DrawOrigin + CorridorFormationRearDirection * 180.0f,
			30.0f,
			FColor::Purple,
			false,
			0.12f,
			0,
			4.0f);
		DrawDebugDirectionalArrow(
			World,
			DrawOrigin,
			DrawOrigin - CorridorFormationRearDirection * 180.0f,
			30.0f,
			FColor::Purple,
			false,
			0.12f,
			0,
			4.0f);
	}
	else if (IsPocketFormationActive())
	{
		DrawDebugDirectionalArrow(
			World,
			DrawOrigin,
			DrawOrigin + PocketFormationDirection * 180.0f,
			30.0f,
			FColor::Orange,
			false,
			0.12f,
			0,
			4.0f);
	}

	const auto ResolveModeText = [](EBHCombatSpaceMode Mode)
	{
		switch (Mode)
		{
		case EBHCombatSpaceMode::Corridor:
			return TEXT("Corridor");
		case EBHCombatSpaceMode::Pocket:
			return TEXT("Pocket");
		default:
			return TEXT("Open");
		}
	};
	const TCHAR* CurrentModeText = ResolveModeText(CurrentSpaceMode);
	const TCHAR* CandidateModeText = ResolveModeText(CandidateSpaceMode);
	const FString SpaceDebugText = FString::Printf(
		TEXT("Space %s | Candidate:%s %.1fs | Width:%.0f Axis:%.0f Ratio:%.2f | PocketArc:%.0f Wall:%.2f | Sides:%d Lanes:%d ActiveA:%d"),
		CurrentModeText,
		CandidateModeText,
		SpaceModeTransitionElapsed,
		EstimatedCorridorWidth,
		EstimatedCorridorAxisLength,
		EstimatedCorridorAspectRatio,
		EstimatedPocketOpenArc,
		EstimatedPocketBlockedFraction,
		IsCorridorFormationActive() ? 2 : 0,
		IsCorridorFormationActive() ? ActiveCorridorLaneCount : 0,
		GetActiveAttackSlotCount());
	DrawDebugString(
		World,
		Owner->GetActorLocation() + FVector(0.0f, 0.0f, 230.0f),
		SpaceDebugText,
		nullptr,
		CurrentSpaceMode == EBHCombatSpaceMode::Corridor
			? FColor::Cyan
			: (CurrentSpaceMode == EBHCombatSpaceMode::Pocket ? FColor::Orange : FColor::White),
		0.12f,
		false,
		1.0f);
}
