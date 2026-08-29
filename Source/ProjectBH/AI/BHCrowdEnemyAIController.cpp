// Copyright ProjectBH. All Rights Reserved.

#include "BHCrowdEnemyAIController.h"

#include "../BHHeroCharacter.h"
#include "../Components/Combat/CombatEngagementSlotComponent.h"
#include "../Enemies/BHEnemy.h"
#include "../ProjectBH.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

void ABHCrowdEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!HasAuthority())
	{
		return;
	}

	ApplyCrowdFollowingSettings();
	FRandomStream RefreshRandom(InPawn ? InPawn->GetUniqueID() : GetUniqueID());
	const float MinimumRefreshInterval = FMath::Max(0.1f, TargetRefreshInterval - TargetRefreshJitter);
	const float MaximumRefreshInterval = FMath::Max(MinimumRefreshInterval, TargetRefreshInterval + TargetRefreshJitter);
	ResolvedTargetRefreshInterval = RefreshRandom.FRandRange(MinimumRefreshInterval, MaximumRefreshInterval);
	const float FirstRefreshDelay = RefreshRandom.FRandRange(0.05f, ResolvedTargetRefreshInterval);
	RefreshTargetAndMove();
	GetWorldTimerManager().SetTimer(
		TargetRefreshTimerHandle,
		this,
		&ThisClass::RefreshTargetAndMove,
		ResolvedTargetRefreshInterval,
		true,
		FirstRefreshDelay);
}

void ABHCrowdEnemyAIController::ApplyCrowdFollowingSettings()
{
	UCrowdFollowingComponent* CrowdFollowing = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent());
	if (!CrowdFollowing)
	{
		UE_LOG(
			LogProjectBH,
			Warning,
			TEXT("%s cannot apply crowd settings because its path following component is not UCrowdFollowingComponent."),
			*GetName());
		return;
	}

	ECrowdAvoidanceQuality::Type ResolvedQuality = ECrowdAvoidanceQuality::High;
	switch (CrowdAvoidanceQuality)
	{
	case EBHCrowdAvoidanceQuality::Low:
		ResolvedQuality = ECrowdAvoidanceQuality::Low;
		break;
	case EBHCrowdAvoidanceQuality::Medium:
		ResolvedQuality = ECrowdAvoidanceQuality::Medium;
		break;
	case EBHCrowdAvoidanceQuality::Good:
		ResolvedQuality = ECrowdAvoidanceQuality::Good;
		break;
	case EBHCrowdAvoidanceQuality::High:
	default:
		ResolvedQuality = ECrowdAvoidanceQuality::High;
		break;
	}

	CrowdFollowing->SetCrowdObstacleAvoidance(bEnableCrowdObstacleAvoidance);
	CrowdFollowing->SetCrowdSeparation(bEnableCrowdSeparation);
	CrowdFollowing->SetCrowdSeparationWeight(FMath::Max(0.0f, CrowdSeparationWeight));
	CrowdFollowing->SetCrowdAvoidanceQuality(ResolvedQuality);
	CrowdFollowing->SetCrowdAnticipateTurns(bEnableCrowdAnticipateTurns);
	CrowdFollowing->SetCrowdCollisionQueryRange(FMath::Max(0.0f, CrowdCollisionQueryRange));
	CrowdFollowing->SetCrowdAvoidanceRangeMultiplier(FMath::Max(0.1f, CrowdAvoidanceRangeMultiplier));

	UE_LOG(
		LogProjectBH,
		Verbose,
		TEXT("%s applied crowd settings. Separation:%s Weight:%.2f Quality:%d QueryRange:%.0f RangeMultiplier:%.2f"),
		*GetName(),
		bEnableCrowdSeparation ? TEXT("On") : TEXT("Off"),
		CrowdSeparationWeight,
		static_cast<int32>(ResolvedQuality),
		CrowdCollisionQueryRange,
		CrowdAvoidanceRangeMultiplier);
}

void ABHCrowdEnemyAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(TargetRefreshTimerHandle);
	ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::UnPossessed);
	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);
	CurrentTarget = nullptr;
	TargetAcquiredTime = 0.0f;
	bInEngagementFormation = false;
	ResetPursuitTracking();

	Super::OnUnPossess();
}

void ABHCrowdEnemyAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);
	if (bHasRequestedPursuitMove)
	{
		bHasRequestedPursuitMove = false;
		if (Result.Code == EPathFollowingResult::Success)
		{
			ResetStuckTracking();
		}
		return;
	}
	if (Result.Code == EPathFollowingResult::Success)
	{
		bHasRequestedSlotMove = false;
		ResetStuckTracking();
		return;
	}

	if (Result.Code != EPathFollowingResult::Success
		&& Result.Code != EPathFollowingResult::Aborted
		&& CurrentSlotType != EBHCombatSlotType::None)
	{
		UE_LOG(
			LogProjectBH,
			Warning,
			TEXT("%s could not reach its reserved combat slot; releasing the reservation."),
			*GetName());
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::PathFollowingFailed, true);
	}
}

void ABHCrowdEnemyAIController::ReleaseCombatSlot(EBHCombatSlotReleaseReason Reason, float ReacquireDelay)
{
	ReleaseCurrentCombatSlot(Reason);
	if (GetWorld() && ReacquireDelay > 0.0f)
	{
		SlotRequestBlockedUntil = FMath::Max(
			SlotRequestBlockedUntil,
			GetWorld()->GetTimeSeconds() + ReacquireDelay);
	}
}

void ABHCrowdEnemyAIController::RefreshTargetAndMove()
{
	ABHEnemy* ControlledEnemy = Cast<ABHEnemy>(GetPawn());
	if (!HasAuthority() || !ControlledEnemy)
	{
		return;
	}

	if (ControlledEnemy->IsAttackLocked())
	{
		StopMovement();
		ApplyMovementIntent(ControlledEnemy, AttackIngressSpeed, true);
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	ABHHeroCharacter* SelectedHero = SelectTargetHero();
	const bool bTargetChanged = SelectedHero != CurrentTarget;
	if (bTargetChanged)
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::TargetChanged);
		SlotRequestBlockedUntil = 0.0f;
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		CurrentTarget = SelectedHero;
		TargetAcquiredTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		bInEngagementFormation = false;
		ResetPursuitTracking();
	}

	if (!IsValid(CurrentTarget))
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::TargetLost);
		StopMovement();
		bInEngagementFormation = false;
		ResetPursuitTracking();
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	if (GetWorld()->GetTimeSeconds() < SlotRequestBlockedUntil)
	{
		StopMovement();
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	const float DistanceToTarget = FVector::Dist2D(
		ControlledEnemy->GetActorLocation(),
		CurrentTarget->GetActorLocation());
	LastDistanceToSlot = DistanceToTarget;
	if (bInEngagementFormation && DistanceToTarget >= FMath::Max(EngagementEnterRadius, EngagementExitRadius))
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::LeftEngagementRange);
		bInEngagementFormation = false;
		ResetPursuitTracking();
	}
	else if (!bInEngagementFormation && DistanceToTarget <= FMath::Max(0.0f, EngagementEnterRadius))
	{
		bInEngagementFormation = true;
		StopMovement();
		ResetPursuitTracking();
	}

	if (!bInEngagementFormation)
	{
		if (CurrentSlotType != EBHCombatSlotType::None)
		{
			ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::LeftEngagementRange);
		}
		ApplyMovementIntent(ControlledEnemy, PursuitSpeed, false);
		RequestPursuitMove(ControlledEnemy);
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	UCombatEngagementSlotComponent* TargetSlotComponent = CurrentTarget->GetCombatEngagementSlotComponent();
	if (!TargetSlotComponent)
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::ReservationInvalid);
		StopMovement();
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	// Initial candidates are registered for fair Attack selection, but keep
	// charging instead of visibly walking to provisional Wait/Holding slots.
	if (TargetSlotComponent->IsInitialFormationPending())
	{
		AcquireCombatSlot(ControlledEnemy);
		ApplyMovementIntent(ControlledEnemy, PursuitSpeed, true);
		RequestPursuitMove(ControlledEnemy, InitialChargeStopRadius);
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	ResetPursuitTracking();
	if (!AcquireCombatSlot(ControlledEnemy))
	{
		StopMovement();
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	FVector SlotLocation;
	const bool bHasCurrentLocation = CurrentSlotType == EBHCombatSlotType::Pending
		? CurrentSlotComponent->GetPendingWaitLocation(GetPawn(), CurrentSlotIndex, SlotLocation)
		: CurrentSlotComponent->GetReservedSlot(GetPawn(), CurrentSlotType, CurrentSlotIndex, SlotLocation);
	if (!bHasCurrentLocation)
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::ReservationInvalid);
		StopMovement();
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	FVector MoveGoal;
	EBHCombatMoveRouteStage ResolvedRouteStage = EBHCombatMoveRouteStage::Direct;
	if (!CurrentSlotComponent->GetMoveGoalForReservedSlot(
		GetPawn(),
		CurrentSlotType,
		CurrentSlotIndex,
		SlotLocation,
		CurrentMoveRouteStage,
		MoveGoal,
		ResolvedRouteStage))
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::ReservationInvalid);
		StopMovement();
		DrawDebugStatus(ControlledEnemy);
		return;
	}
	CurrentMoveRouteStage = ResolvedRouteStage;

	const float DistanceToSlotSquared = FVector::DistSquared2D(GetPawn()->GetActorLocation(), SlotLocation);
	const float DistanceToSlot = FMath::Sqrt(DistanceToSlotSquared);
	const float DistanceToMoveGoal = FVector::Dist2D(GetPawn()->GetActorLocation(), MoveGoal);
	LastDistanceToSlot = DistanceToSlot;
	const bool bAtReservedSlot = DistanceToSlotSquared <= FMath::Square(SlotAcceptanceRadius);
	const float CurrentMoveSpeed = ControlledEnemy->GetVelocity().Size2D();
	const bool bCanSettleAtReservedSlot = bAtReservedSlot
		&& CurrentMoveSpeed <= FMath::Max(0.0f, SlotSettleSpeedThreshold);
	if (CurrentSlotType == EBHCombatSlotType::Attack && bAtReservedSlot)
	{
		ApplyMovementIntent(ControlledEnemy, AttackIngressSpeed, true);
		if (!bCanSettleAtReservedSlot)
		{
			// Do not replace the completed path with another zero-length request.
			// CharacterMovement is still braking and will settle on a later refresh.
			DrawDebugStatus(ControlledEnemy);
			return;
		}

		StopMovement();
		bHasRequestedSlotMove = false;
		bIsReforming = false;
		ResetStuckTracking();

		const float AttackStartRange = ControlledEnemy->GetAttackStartRange();
		const float DistanceToTargetSquared = FVector::DistSquared2D(GetPawn()->GetActorLocation(), CurrentTarget->GetActorLocation());
		if (DistanceToTargetSquared <= FMath::Square(AttackStartRange))
		{
			if (!ControlledEnemy->TryStartBasicAttack(CurrentTarget))
			{
				ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::AttackStartFailed, true);
			}
		}
		else
		{
			ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::AttackRangeMismatch, true);
		}
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	if (bAtReservedSlot)
	{
		ApplyMovementIntent(ControlledEnemy, GetCurrentSlotMoveSpeed(), true);
		if (!bCanSettleAtReservedSlot)
		{
			DrawDebugStatus(ControlledEnemy);
			return;
		}

		StopMovement();
		bHasRequestedSlotMove = false;
		bIsReforming = false;
		ResetStuckTracking();
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	ApplyMovementIntent(
		ControlledEnemy,
		GetCurrentSlotMoveSpeed(),
		true);

	if (bHasRequestedSlotMove
		&& UpdateStuckTracking(DistanceToMoveGoal, ControlledEnemy->GetVelocity().Size2D()))
	{
		StopMovement();
		if (CurrentSlotType == EBHCombatSlotType::Attack
			&& CurrentSlotComponent->HandleStalledAttackReservation(
				ControlledEnemy,
				FailedSlotCooldown))
		{
			LastReleaseReason = EBHCombatSlotReleaseReason::Stalled;
			bHasRequestedSlotMove = false;
			CurrentMoveRouteStage = EBHCombatMoveRouteStage::Direct;
			LastRequestedRouteStage = EBHCombatMoveRouteStage::Direct;
			ResetStuckTracking();

			FVector DemotedSlotLocation;
			CurrentSlotComponent->GetReservedSlot(
				ControlledEnemy,
				CurrentSlotType,
				CurrentSlotIndex,
				DemotedSlotLocation);
			TrackedSlotType = CurrentSlotType;
			TrackedSlotIndex = CurrentSlotIndex;
			DrawDebugStatus(ControlledEnemy);
			return;
		}

		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::Stalled, true);
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	const bool bRouteStageChanged = CurrentMoveRouteStage != LastRequestedRouteStage;
	const bool bSlotMoved = !bHasRequestedSlotMove
		|| FVector::DistSquared2D(LastRequestedSlotLocation, MoveGoal) >= FMath::Square(GetCurrentSlotRepathDistance());
	if (bTargetChanged || bRouteStageChanged || bSlotMoved || GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		const bool bUsesRingWaypoint = CurrentMoveRouteStage == EBHCombatMoveRouteStage::ApproachRing
			|| CurrentMoveRouteStage == EBHCombatMoveRouteStage::AlignOnRing;
		RequestMoveToReservedSlot(
			MoveGoal,
			bUsesRingWaypoint ? RingWaypointAcceptanceRadius : SlotAcceptanceRadius);
	}

	DrawDebugStatus(ControlledEnemy);
}

ABHHeroCharacter* ABHCrowdEnemyAIController::SelectTargetHero() const
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !GetWorld())
	{
		return nullptr;
	}
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (IsValid(CurrentTarget)
		&& CurrentTarget->IsPlayerControlled()
		&& CurrentTime - TargetAcquiredTime < MinimumTargetHoldTime
		&& IsHeroReachable(CurrentTarget))
	{
		return CurrentTarget.Get();
	}

	ABHHeroCharacter* ClosestReachableHero = nullptr;
	float ClosestReachableDistance = TNumericLimits<float>::Max();
	bool bCurrentTargetReachable = false;
	float CurrentTargetDistance = TNumericLimits<float>::Max();

	for (TActorIterator<ABHHeroCharacter> It(GetWorld()); It; ++It)
	{
		ABHHeroCharacter* Hero = *It;
		if (!IsValid(Hero) || !Hero->IsPlayerControlled() || !IsHeroReachable(Hero))
		{
			continue;
		}

		const float Distance = FVector::Dist2D(ControlledPawn->GetActorLocation(), Hero->GetActorLocation());
		if (Hero == CurrentTarget)
		{
			bCurrentTargetReachable = true;
			CurrentTargetDistance = Distance;
		}
		if (Distance < ClosestReachableDistance)
		{
			ClosestReachableDistance = Distance;
			ClosestReachableHero = Hero;
		}
	}

	if (!bCurrentTargetReachable || !IsValid(CurrentTarget))
	{
		return ClosestReachableHero;
	}

	if (CurrentTime - TargetAcquiredTime < MinimumTargetHoldTime
		|| ClosestReachableHero == CurrentTarget)
	{
		return CurrentTarget;
	}

	return ClosestReachableHero
		&& ClosestReachableDistance + TargetSwitchDistanceAdvantage < CurrentTargetDistance
		? ClosestReachableHero
		: CurrentTarget.Get();
}

bool ABHCrowdEnemyAIController::IsHeroReachable(const ABHHeroCharacter* Hero) const
{
	const APawn* ControlledPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!ControlledPawn || !IsValid(Hero) || !World)
	{
		return false;
	}

	const UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavigationSystem)
	{
		return false;
	}

	FNavLocation ProjectedTargetLocation;
	if (!NavigationSystem->ProjectPointToNavigation(
		Hero->GetActorLocation(),
		ProjectedTargetLocation,
		TargetNavProjectionExtent))
	{
		return false;
	}

	UNavigationPath* NavigationPath = UNavigationSystemV1::FindPathToLocationSynchronously(
		World,
		ControlledPawn->GetActorLocation(),
		ProjectedTargetLocation.Location,
		const_cast<APawn*>(ControlledPawn));
	return NavigationPath && NavigationPath->IsValid() && !NavigationPath->IsPartial();
}

bool ABHCrowdEnemyAIController::AcquireCombatSlot(ABHEnemy* ControlledEnemy)
{
	if (!ControlledEnemy || !IsValid(CurrentTarget))
	{
		return false;
	}

	UCombatEngagementSlotComponent* TargetSlotComponent = CurrentTarget->GetCombatEngagementSlotComponent();
	if (!TargetSlotComponent)
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::ReservationInvalid);
		return false;
	}

	if (CurrentSlotComponent && CurrentSlotComponent != TargetSlotComponent)
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::TargetChanged);
	}
	if (CurrentSlotComponent != TargetSlotComponent)
	{
		CurrentSlotComponent = TargetSlotComponent;
		LastObservedFormationRevision = CurrentSlotComponent->GetFormationRevision();
	}
	else if (LastObservedFormationRevision != CurrentSlotComponent->GetFormationRevision())
	{
		LastObservedFormationRevision = CurrentSlotComponent->GetFormationRevision();
		++ReformCount;
		bIsReforming = true;
		StopMovement();
		bHasRequestedSlotMove = false;
		CurrentMoveRouteStage = EBHCombatMoveRouteStage::Direct;
		LastRequestedRouteStage = EBHCombatMoveRouteStage::Direct;
		ResetStuckTracking();
	}

	const bool bHasActiveExclusion = GetWorld()
		&& GetWorld()->GetTimeSeconds() < ExcludedSlotUntil;
	CurrentSlotComponent->RequestEngagementSlot(
		ControlledEnemy,
		bHasActiveExclusion ? ExcludedSlotType : EBHCombatSlotType::None,
		bHasActiveExclusion ? ExcludedSlotIndex : INDEX_NONE);

	FVector ResolvedSlotLocation;
	const bool bHasReservation = CurrentSlotComponent->GetReservedSlot(
		ControlledEnemy,
		CurrentSlotType,
		CurrentSlotIndex,
		ResolvedSlotLocation);
	const bool bHasPendingLocation = !bHasReservation
		&& CurrentSlotComponent->GetPendingWaitLocation(
			ControlledEnemy,
			CurrentSlotIndex,
			ResolvedSlotLocation);
	if (bHasPendingLocation)
	{
		CurrentSlotType = EBHCombatSlotType::Pending;
	}

	// Keep the requester even while it is pending beyond Holding capacity so
	// UnPossess/target loss can remove its central queue entry immediately.
	CurrentSlotRequester = ControlledEnemy;
	if ((bHasReservation || bHasPendingLocation)
		&& (TrackedSlotType != CurrentSlotType || TrackedSlotIndex != CurrentSlotIndex))
	{
		bHasRequestedSlotMove = false;
		CurrentMoveRouteStage = EBHCombatMoveRouteStage::Direct;
		LastRequestedRouteStage = EBHCombatMoveRouteStage::Direct;
		ResetStuckTracking();
		TrackedSlotType = CurrentSlotType;
		TrackedSlotIndex = CurrentSlotIndex;
	}
	return bHasReservation || bHasPendingLocation;
}

void ABHCrowdEnemyAIController::RequestPursuitMove(
	ABHEnemy* ControlledEnemy,
	float AcceptanceRadius)
{
	if (!ControlledEnemy || !IsValid(CurrentTarget) || !GetWorld())
	{
		return;
	}

	FVector PursuitGoal = CurrentTarget->GetActorLocation()
		+ CurrentTarget->GetVelocity() * FMath::Max(0.0f, PursuitPredictionTime);
	if (const UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation ProjectedGoal;
		if (NavigationSystem->ProjectPointToNavigation(PursuitGoal, ProjectedGoal, TargetNavProjectionExtent))
		{
			PursuitGoal = ProjectedGoal.Location;
		}
	}

	const bool bGoalMoved = !bHasRequestedPursuitMove
		|| FVector::DistSquared2D(LastRequestedPursuitLocation, PursuitGoal)
			>= FMath::Square(FMath::Max(1.0f, PursuitRepathDistance));
	if (!bGoalMoved && GetMoveStatus() == EPathFollowingStatus::Moving)
	{
		return;
	}

	const EPathFollowingRequestResult::Type RequestResult = MoveToLocation(
		PursuitGoal,
		FMath::Max(0.0f, AcceptanceRadius),
		false,
		true,
		true,
		true,
		nullptr,
		false);
	if (RequestResult == EPathFollowingRequestResult::Failed)
	{
		bHasRequestedPursuitMove = false;
		UE_LOG(
			LogProjectBH,
			Verbose,
			TEXT("%s could not path toward pursuit goal for %s."),
			*GetName(),
			*CurrentTarget->GetName());
		return;
	}

	LastRequestedPursuitLocation = PursuitGoal;
	bHasRequestedPursuitMove = true;
	bHasRequestedSlotMove = false;
}

void ABHCrowdEnemyAIController::ApplyMovementIntent(
	ABHEnemy* ControlledEnemy,
	float MoveSpeed,
	bool bFaceTarget)
{
	if (!ControlledEnemy)
	{
		return;
	}

	if (UCharacterMovementComponent* Movement = ControlledEnemy->GetCharacterMovement())
	{
		// Path following normally writes velocity directly. Acceleration-based paths
		// let CharacterMovement brake and accelerate instead of changing speed in steps.
		if (FNavMovementProperties* NavMovementProperties = Movement->GetNavMovementProperties())
		{
			NavMovementProperties->bUseAccelerationForPaths = true;
		}
		Movement->bRequestedMoveUseAcceleration = true;
		Movement->MaxWalkSpeed = FMath::Max(0.0f, MoveSpeed);
		Movement->bOrientRotationToMovement = !bFaceTarget;
		Movement->bUseControllerDesiredRotation = bFaceTarget;
	}
	ControlledEnemy->bUseControllerRotationYaw = false;

	if (bFaceTarget && IsValid(CurrentTarget))
	{
		SetFocus(CurrentTarget, EAIFocusPriority::Gameplay);
	}
	else
	{
		ClearFocus(EAIFocusPriority::Gameplay);
	}
}

float ABHCrowdEnemyAIController::GetCurrentSlotMoveSpeed() const
{
	switch (CurrentSlotType)
	{
	case EBHCombatSlotType::Attack:
		return AttackIngressSpeed;
	case EBHCombatSlotType::Wait:
		return WaitMoveSpeed;
	case EBHCombatSlotType::Holding:
	case EBHCombatSlotType::Pending:
		return HoldingMoveSpeed;
	case EBHCombatSlotType::None:
	default:
		return PursuitSpeed;
	}
}

float ABHCrowdEnemyAIController::GetCurrentSlotRepathDistance() const
{
	switch (CurrentSlotType)
	{
	case EBHCombatSlotType::Attack:
		return FMath::Max(1.0f, SlotRepathDistance);
	case EBHCombatSlotType::Wait:
		return FMath::Max(1.0f, WaitSlotRepathDistance);
	case EBHCombatSlotType::Holding:
	case EBHCombatSlotType::Pending:
		return FMath::Max(1.0f, HoldingSlotRepathDistance);
	case EBHCombatSlotType::None:
	default:
		return FMath::Max(1.0f, PursuitRepathDistance);
	}
}

void ABHCrowdEnemyAIController::ResetPursuitTracking()
{
	bHasRequestedPursuitMove = false;
	LastRequestedPursuitLocation = FVector::ZeroVector;
}

void ABHCrowdEnemyAIController::ReleaseCurrentCombatSlot(
	EBHCombatSlotReleaseReason Reason,
	bool bTemporarilyExcludeReleasedSlot)
{
	const bool bHadReservation = CurrentSlotType != EBHCombatSlotType::None && CurrentSlotIndex != INDEX_NONE;
	if (bTemporarilyExcludeReleasedSlot && bHadReservation && GetWorld())
	{
		ExcludedSlotType = CurrentSlotType;
		ExcludedSlotIndex = CurrentSlotIndex;
		ExcludedSlotUntil = GetWorld()->GetTimeSeconds() + FailedSlotCooldown;
	}
	else if (Reason == EBHCombatSlotReleaseReason::TargetChanged
		|| Reason == EBHCombatSlotReleaseReason::TargetLost)
	{
		ExcludedSlotType = EBHCombatSlotType::None;
		ExcludedSlotIndex = INDEX_NONE;
		ExcludedSlotUntil = 0.0f;
	}

	if (CurrentSlotComponent && CurrentSlotRequester.IsValid())
	{
		CurrentSlotComponent->ReleaseSlot(
			CurrentSlotRequester.Get(),
			ShouldPreserveQueuePosition(Reason));
	}
	if (bHadReservation)
	{
		UE_LOG(
			LogProjectBH,
			Display,
			TEXT("%s released %s slot %d. Reason: %s"),
			*GetName(),
			*StaticEnum<EBHCombatSlotType>()->GetNameStringByValue(static_cast<int64>(CurrentSlotType)),
			CurrentSlotIndex,
			*StaticEnum<EBHCombatSlotReleaseReason>()->GetNameStringByValue(static_cast<int64>(Reason)));
	}

	if (bHadReservation)
	{
		LastReleaseReason = Reason;
	}
	CurrentSlotComponent = nullptr;
	CurrentSlotType = EBHCombatSlotType::None;
	CurrentSlotIndex = INDEX_NONE;
	CurrentSlotRequester.Reset();
	LastObservedFormationRevision = INDEX_NONE;
	bIsReforming = false;
	CurrentMoveRouteStage = EBHCombatMoveRouteStage::Direct;
	LastRequestedRouteStage = EBHCombatMoveRouteStage::Direct;
	LastRequestedSlotLocation = FVector::ZeroVector;
	bHasRequestedSlotMove = false;
	TrackedSlotType = EBHCombatSlotType::None;
	TrackedSlotIndex = INDEX_NONE;
	ResetStuckTracking();
}

bool ABHCrowdEnemyAIController::ShouldPreserveQueuePosition(
	EBHCombatSlotReleaseReason Reason) const
{
	switch (Reason)
	{
	case EBHCombatSlotReleaseReason::ReservationInvalid:
	case EBHCombatSlotReleaseReason::AttackRangeMismatch:
	case EBHCombatSlotReleaseReason::AttackStartFailed:
	case EBHCombatSlotReleaseReason::MoveRequestFailed:
	case EBHCombatSlotReleaseReason::PathFollowingFailed:
	case EBHCombatSlotReleaseReason::Stalled:
		return true;
	case EBHCombatSlotReleaseReason::None:
	case EBHCombatSlotReleaseReason::TargetChanged:
	case EBHCombatSlotReleaseReason::TargetLost:
	case EBHCombatSlotReleaseReason::UnPossessed:
	case EBHCombatSlotReleaseReason::AttackRecoveryComplete:
	case EBHCombatSlotReleaseReason::Staggered:
	case EBHCombatSlotReleaseReason::Died:
	default:
		return false;
	}
}

void ABHCrowdEnemyAIController::RequestMoveToReservedSlot(
	const FVector& SlotLocation,
	float AcceptanceRadius)
{
	const EPathFollowingRequestResult::Type RequestResult = MoveToLocation(
		SlotLocation,
		AcceptanceRadius,
		false,
		true,
		true,
		true,
		nullptr,
		false);

	if (RequestResult == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(
			LogProjectBH,
			Warning,
			TEXT("%s failed to find a NavMesh path to its reserved combat slot for %s."),
			*GetName(),
			IsValid(CurrentTarget) ? *CurrentTarget->GetName() : TEXT("invalid target"));
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::MoveRequestFailed, true);
		return;
	}

	LastRequestedSlotLocation = SlotLocation;
	LastRequestedRouteStage = CurrentMoveRouteStage;
	bHasRequestedSlotMove = true;
}

bool ABHCrowdEnemyAIController::UpdateStuckTracking(float DistanceToSlot, float Speed)
{
	if (!bHasProgressSample)
	{
		bHasProgressSample = true;
		ProgressReferenceDistance = DistanceToSlot;
		StuckElapsed = 0.0f;
		return false;
	}

	if (ProgressReferenceDistance - DistanceToSlot >= StuckProgressDistance)
	{
		ProgressReferenceDistance = DistanceToSlot;
		StuckElapsed = 0.0f;
		return false;
	}

	if (Speed < StuckSpeedThreshold)
	{
		StuckElapsed += ResolvedTargetRefreshInterval;
	}
	else
	{
		StuckElapsed = 0.0f;
	}

	return StuckElapsed >= StuckTimeout;
}

void ABHCrowdEnemyAIController::ResetStuckTracking()
{
	ProgressReferenceDistance = 0.0f;
	StuckElapsed = 0.0f;
	LastDistanceToSlot = 0.0f;
	bHasProgressSample = false;
}

void ABHCrowdEnemyAIController::DrawDebugStatus(const ABHEnemy* ControlledEnemy) const
{
	if (!bDrawCrowdDebug || !ControlledEnemy || !GetWorld())
	{
		return;
	}

	const FString CombatStateName = StaticEnum<EBHEnemyCombatState>()->GetNameStringByValue(
		static_cast<int64>(ControlledEnemy->GetCombatState()));
	const FString SlotName = StaticEnum<EBHCombatSlotType>()->GetNameStringByValue(
		static_cast<int64>(CurrentSlotType));
	const FString ReleaseName = StaticEnum<EBHCombatSlotReleaseReason>()->GetNameStringByValue(
		static_cast<int64>(LastReleaseReason));
	const FString RouteStageName = StaticEnum<EBHCombatMoveRouteStage>()->GetNameStringByValue(
		static_cast<int64>(CurrentMoveRouteStage));
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	const float TargetHeldTime = IsValid(CurrentTarget)
		? FMath::Max(0.0f, CurrentTime - TargetAcquiredTime)
		: 0.0f;
	const uint64 QueueSequence = CurrentSlotComponent && CurrentSlotRequester.IsValid()
		? CurrentSlotComponent->GetQueueSequenceForRequester(CurrentSlotRequester.Get())
		: 0;
	FColor RouteColor = FColor::White;
	switch (CurrentMoveRouteStage)
	{
	case EBHCombatMoveRouteStage::ApproachRing:
		RouteColor = FColor::Cyan;
		break;
	case EBHCombatMoveRouteStage::AlignOnRing:
		RouteColor = FColor::Blue;
		break;
	case EBHCombatMoveRouteStage::Ingress:
		RouteColor = FColor::Orange;
		break;
	case EBHCombatMoveRouteStage::Direct:
	default:
		break;
	}
	if (bHasRequestedSlotMove)
	{
		DrawDebugLine(
			GetWorld(),
			ControlledEnemy->GetActorLocation(),
			LastRequestedSlotLocation,
			RouteColor,
			false,
			ResolvedTargetRefreshInterval + 0.1f,
			0,
			2.0f);
		DrawDebugSphere(
			GetWorld(),
			LastRequestedSlotLocation,
			10.0f,
			8,
			RouteColor,
			false,
			ResolvedTargetRefreshInterval + 0.1f,
			0,
			1.5f);
	}
	else if (bHasRequestedPursuitMove)
	{
		DrawDebugLine(
			GetWorld(),
			ControlledEnemy->GetActorLocation(),
			LastRequestedPursuitLocation,
			FColor::Green,
			false,
			ResolvedTargetRefreshInterval + 0.1f,
			0,
			2.0f);
	}
	const bool bInitialCharge = bInEngagementFormation
		&& CurrentSlotComponent
		&& CurrentSlotComponent->IsInitialFormationPending();
	const TCHAR* MovementMode = !bInEngagementFormation
		? TEXT("Pursuit")
		: (bInitialCharge ? TEXT("InitialCharge") : TEXT("Formation"));
	const UCharacterMovementComponent* CharacterMovement = ControlledEnemy->GetCharacterMovement();
	const TCHAR* FacingMode = CharacterMovement && CharacterMovement->bUseControllerDesiredRotation
		? TEXT("Target")
		: TEXT("Move");
	const FString DebugText = FString::Printf(
		TEXT("%s/%s %.2fs | Target:%s Held:%.1f | %s[%d] Seq:%llu Dist:%.0f Speed:%.1f Facing:%s Route:%s Stuck:%.1f Starts:%d | Reform:%s(%d) | Last:%s"),
		*CombatStateName,
		MovementMode,
		ResolvedTargetRefreshInterval,
		IsValid(CurrentTarget) ? *CurrentTarget->GetName() : TEXT("None"),
		TargetHeldTime,
		*SlotName,
		CurrentSlotIndex,
		static_cast<unsigned long long>(QueueSequence),
		LastDistanceToSlot,
		ControlledEnemy->GetVelocity().Size2D(),
		FacingMode,
		*RouteStageName,
		StuckElapsed,
		ControlledEnemy->GetSuccessfulAttackStartCount(),
		bIsReforming ? TEXT("Yes") : TEXT("No"),
		ReformCount,
		*ReleaseName);
	DrawDebugString(
		GetWorld(),
		ControlledEnemy->GetActorLocation() + FVector(0.0f, 0.0f, 125.0f),
		DebugText,
		nullptr,
		CurrentSlotType == EBHCombatSlotType::Attack
			? FColor::Red
			: (CurrentSlotType == EBHCombatSlotType::Wait
				? FColor::Yellow
				: (CurrentSlotType == EBHCombatSlotType::Holding
					? FColor::Purple
					: (CurrentSlotType == EBHCombatSlotType::Pending ? FColor(0, 200, 120) : FColor::Silver))),
		ResolvedTargetRefreshInterval + 0.1f,
		false,
		0.85f);
}
