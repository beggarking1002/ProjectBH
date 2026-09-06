// Copyright ProjectBH. All Rights Reserved.

#include "BHCrowdEnemyAIController.h"
#include "../Diagnostics/BHCombatDiagnosticsSubsystem.h"

#include "Navigation/BHNavigationQuery.h"
#include "../BHHeroCharacter.h"
#include "../Components/Combat/CombatEngagementSlotComponent.h"
#include "../Debug/BHDebugDraw.h"
#include "../Enemies/BHEnemy.h"
#include "../ProjectBH.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

namespace
{
constexpr int32 NormalEnemyAvoidanceGroup = 1 << 0;
constexpr int32 LargeEnemyAvoidanceGroup = 1 << 1;

bool IsIntermediateCombatRouteStage(EBHCombatMoveRouteStage RouteStage)
{
	switch (RouteStage)
	{
	case EBHCombatMoveRouteStage::ExitDecompression:
	case EBHCombatMoveRouteStage::ApproachRing:
	case EBHCombatMoveRouteStage::AlignOnRing:
	case EBHCombatMoveRouteStage::BypassCorePositive:
	case EBHCombatMoveRouteStage::BypassCoreNegative:
	case EBHCombatMoveRouteStage::CoreEscape:
		return true;
	case EBHCombatMoveRouteStage::Direct:
	case EBHCombatMoveRouteStage::Ingress:
	default:
		return false;
	}
}
}

void ABHCrowdEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!HasAuthority())
	{
		return;
	}

	if (ABHEnemy* ControlledEnemy = Cast<ABHEnemy>(InPawn))
	{
		ControlledEnemy->ResetFormationJoinState();
	}
	PursuitDistributionSeed = InPawn
		? static_cast<uint32>(InPawn->GetUniqueID())
		: static_cast<uint32>(GetUniqueID());
	SmoothedPursuitForward = FVector::ZeroVector;
	ActivePursuitLaneIndex = INDEX_NONE;
	ActivePursuitRowIndex = INDEX_NONE;

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
	CrowdFollowing->SetCrowdSlowdownAtGoal(true);

	// Normal agents account for both groups, while a Large agent only
	// accounts for other Large agents. This makes the small crowd yield locally
	// without making the Troll weave or stop behind it.
	const ABHEnemy* ControlledEnemy = Cast<ABHEnemy>(GetPawn());
	const bool bIsLargeEnemy = ControlledEnemy
		&& ControlledEnemy->GetEnemySizeClass() == EBHEnemySizeClass::Large;
	CrowdFollowing->SetAvoidanceGroup(
		bIsLargeEnemy ? LargeEnemyAvoidanceGroup : NormalEnemyAvoidanceGroup);
	CrowdFollowing->SetGroupsToAvoid(
		bIsLargeEnemy
			? LargeEnemyAvoidanceGroup
			: (NormalEnemyAvoidanceGroup | LargeEnemyAvoidanceGroup));
	CrowdFollowing->SetGroupsToIgnore(
		bIsLargeEnemy ? NormalEnemyAvoidanceGroup : 0);

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
	ClearWaitIngressDeferral();
	FinishTemporaryHoldingYield(false);
	FinishOverlapRecovery(false);
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
	if (bHoldingYieldActive && RequestID == HoldingYieldRequestID)
	{
		HoldingYieldRequestID = FAIRequestID::InvalidRequest;
		if (Result.Code == EPathFollowingResult::Success)
		{
			bHoldingYieldAtGoal = true;
		}
		else
		{
			FinishTemporaryHoldingYield(false);
			NotifyCombatSlotAssignmentChanged();
		}
		return;
	}
	if (bOverlapRecoveryActive && RequestID == OverlapRecoveryRequestID)
	{
		FinishOverlapRecovery(false);
		NotifyCombatSlotAssignmentChanged();
		return;
	}
	if (bHasRequestedPursuitMove)
	{
		bHasRequestedPursuitMove = false;
		if (Result.Code == EPathFollowingResult::Success)
		{
			ResetPursuitStuckWatchdog();
			ResetStuckTracking();
		}
		return;
	}
	if (Result.Code == EPathFollowingResult::Success)
	{
		bHasRequestedSlotMove = false;
		ResetStuckTracking();
		if (IsIntermediateCombatRouteStage(CurrentMoveRouteStage)
			&& !bIsChainingIntermediateRoute
			&& CurrentSlotType != EBHCombatSlotType::None
			&& IsValid(CurrentSlotComponent))
		{
			TGuardValue<bool> ChainingGuard(bIsChainingIntermediateRoute, true);
			RefreshTargetAndMove();
		}
		return;
	}

	if (Result.Code != EPathFollowingResult::Success
		&& Result.Code != EPathFollowingResult::Aborted
		&& CurrentSlotType != EBHCombatSlotType::None)
	{
		UE_LOG(
			LogProjectBH,
			Warning,
			TEXT("%s could not reach its reserved combat slot; attempting slot recovery."),
			*GetName());
		if (TryStartHighGroundDropRecovery(Cast<ABHEnemy>(GetPawn())))
		{
			return;
		}
		if (CurrentSlotType == EBHCombatSlotType::Attack)
		{
			RecoverStalledCombatSlot(Cast<ABHEnemy>(GetPawn()));
		}
		else
		{
			ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::PathFollowingFailed, true);
		}
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

void ABHCrowdEnemyAIController::NotifyCombatSlotAssignmentChanged()
{
	if (!HasAuthority() || bSlotAssignmentRefreshQueued || !GetWorld())
	{
		return;
	}

	bSlotAssignmentRefreshQueued = true;
	GetWorldTimerManager().SetTimerForNextTick(
		this,
		&ThisClass::RefreshAfterCombatSlotAssignmentChanged);
}

bool ABHCrowdEnemyAIController::RequestTemporaryHoldingYield(
	AActor* PassingRequester,
	const FVector& PassingPathDirection,
	const FVector& YieldGoal)
{
	ABHEnemy* ControlledEnemy = Cast<ABHEnemy>(GetPawn());
	if (!HasAuthority()
		|| !ControlledEnemy
		|| !GetWorld()
		|| !PassingRequester
		|| bHoldingYieldActive
		|| bOverlapRecoveryActive
		|| ControlledEnemy->IsAttackLocked()
		|| CurrentSlotType != EBHCombatSlotType::Holding
		|| CurrentFormationMovementRole != EBHFormationMovementRole::StationaryHolding
		|| GetWorld()->GetTimeSeconds() < HoldingYieldRequestCooldownUntil)
	{
		return false;
	}

	FVector PassingDirection = PassingPathDirection;
	PassingDirection.Z = 0.0f;
	if (!PassingDirection.Normalize())
	{
		return false;
	}

	StopMovement();
	bHasRequestedSlotMove = false;
	ResetStuckTracking();

	bHoldingYieldActive = true;
	bHoldingYieldAtGoal = false;
	HoldingYieldGoal = YieldGoal;
	HoldingYieldOrigin = ControlledEnemy->GetActorLocation();
	HoldingYieldPassingDirection = PassingDirection;
	HoldingYieldPassingRequester = PassingRequester;
	HoldingYieldStartTime = GetWorld()->GetTimeSeconds();
	HoldingYieldRequestID = FAIRequestID::InvalidRequest;
	CurrentFormationMovementRole = EBHFormationMovementRole::HoldingYield;
	ControlledEnemy->SetWantsRunLocomotion(false);
	ControlledEnemy->SetFormationCatchUpRequired(false);
	ApplyMovementIntent(ControlledEnemy, HoldingMoveSpeed, true);

	FAIMoveRequest YieldRequest(YieldGoal);
	YieldRequest.SetAcceptanceRadius(FMath::Max(1.0f, HoldingYieldAcceptanceRadius));
	YieldRequest.SetReachTestIncludesAgentRadius(false);
	YieldRequest.SetReachTestIncludesGoalRadius(false);
	YieldRequest.SetUsePathfinding(true);
	YieldRequest.SetAllowPartialPath(false);
	YieldRequest.SetProjectGoalLocation(false);
	YieldRequest.SetCanStrafe(true);
	const FPathFollowingRequestResult RequestResult = MoveTo(YieldRequest);
	if (RequestResult.Code == EPathFollowingRequestResult::Failed)
	{
		FinishTemporaryHoldingYield(false);
		return false;
	}
	if (RequestResult.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		bHoldingYieldAtGoal = true;
	}
	else
	{
		HoldingYieldRequestID = RequestResult.MoveId;
	}

	UE_LOG(
		LogProjectBH,
		Verbose,
		TEXT("%s temporarily yielded its Holding path for %s without releasing the reservation."),
		*GetName(),
		*PassingRequester->GetName());
	return true;
}

void ABHCrowdEnemyAIController::RefreshAfterCombatSlotAssignmentChanged()
{
	bSlotAssignmentRefreshQueued = false;
	// A formation revision often only nudges the same reserved destination. Force
	// an immediate repath without erasing no-progress evidence. AcquireCombatSlot
	// still resets tracking when the actual slot type or index changes.
	bForceSlotPathRefresh = true;
	RefreshTargetAndMove();
}

void ABHCrowdEnemyAIController::RefreshTargetAndMove()
{
	ABHEnemy* ControlledEnemy = Cast<ABHEnemy>(GetPawn());
	if (!HasAuthority() || !ControlledEnemy)
	{
		return;
	}
	// Each refresh starts from a non-running presentation intent. Only the
	// concrete pursuit/Attack ingress paths below opt back into Run.
	ControlledEnemy->SetWantsRunLocomotion(false);
	CurrentFormationMovementRole = EBHFormationMovementRole::None;
	if (ControlledEnemy->IsChargeAttackActive())
	{
		// Root Motion owns displacement and facing for the entire Charge Montage.
		// Do not let path following or focus rotation compete with it.
		ClearFocus(EAIFocusPriority::Gameplay);
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	const bool bTryChargeBeforeRecovery = ControlledEnemy->GetEnemySizeClass() == EBHEnemySizeClass::Large;
	if (bHoldingYieldActive)
	{
		if (ControlledEnemy->IsAttackLocked())
		{
			FinishTemporaryHoldingYield(true);
		}
		else if (!bTryChargeBeforeRecovery && UpdateTemporaryHoldingYield(ControlledEnemy))
		{
			DrawDebugStatus(ControlledEnemy);
			return;
		}
	}

	if (bOverlapRecoveryActive)
	{
		if (ControlledEnemy->IsAttackLocked())
		{
			FinishOverlapRecovery(true);
		}
		else if (!bTryChargeBeforeRecovery && UpdateOverlapRecovery(ControlledEnemy))
		{
			DrawDebugStatus(ControlledEnemy);
			return;
		}
	}

	if (ControlledEnemy->IsHighGroundDropActive())
	{
		if (IsValid(CurrentTarget))
		{
			SetFocus(CurrentTarget, EAIFocusPriority::Gameplay);
		}
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	if (ControlledEnemy->IsAttackLocked() && !ControlledEnemy->CanMoveDuringAttack())
	{
		if (ControlledEnemy->GetCombatState() == EBHEnemyCombatState::Recovering
			&& CurrentSlotType == EBHCombatSlotType::Attack
			&& IsValid(CurrentSlotComponent))
		{
			EBHCombatSlotType ResolvedSlotType = EBHCombatSlotType::None;
			int32 ResolvedSlotIndex = INDEX_NONE;
			FVector ReservedSlotLocation;
			if (CurrentSlotComponent->GetReservedSlot(
				ControlledEnemy,
				ResolvedSlotType,
				ResolvedSlotIndex,
				ReservedSlotLocation))
			{
				CurrentSlotType = ResolvedSlotType;
				CurrentSlotIndex = ResolvedSlotIndex;
				LastDistanceToSlot = FVector::Dist2D(
					ControlledEnemy->GetActorLocation(),
					ReservedSlotLocation);
				if (CurrentSlotType == EBHCombatSlotType::Attack
					&& LastDistanceToSlot > FMath::Max(
						SlotAcceptanceRadius,
						RecoveringAttackSlotLeashDistance))
				{
					StopMovement();
					RecoverStalledCombatSlot(ControlledEnemy);
					DrawDebugStatus(ControlledEnemy);
					return;
				}
			}
		}

		StopMovement();
		ApplyMovementIntent(ControlledEnemy, AttackIngressSpeed, true);
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	if (!bTryChargeBeforeRecovery && TryStartOverlapRecovery(ControlledEnemy))
	{
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	ABHHeroCharacter* SelectedHero = ControlledEnemy->CanMoveDuringAttack()
		? CurrentTarget.Get()
		: SelectTargetHero();
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

	const float DistanceToTarget = FVector::Dist2D(
		ControlledEnemy->GetActorLocation(),
		CurrentTarget->GetActorLocation());
	LastDistanceToSlot = DistanceToTarget;
	// A validated charge is a range-based action, not an Attack-slot reward.
	// Try it before crowd recovery/decompression can indefinitely delay the Troll.
	if (ControlledEnemy->TryStartChargeAttack(CurrentTarget))
	{
		FinishTemporaryHoldingYield(false);
		FinishOverlapRecovery(false);
		ResetPursuitTracking();
		ResetStuckTracking();
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	if (bTryChargeBeforeRecovery
		&& ((bHoldingYieldActive && UpdateTemporaryHoldingYield(ControlledEnemy))
		|| (bOverlapRecoveryActive && UpdateOverlapRecovery(ControlledEnemy))
		|| TryStartOverlapRecovery(ControlledEnemy)))
	{
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	if (GetWorld()->GetTimeSeconds() < SlotRequestBlockedUntil)
	{
		StopMovement();
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	float EffectiveEngagementExitRadius = FMath::Max(EngagementEnterRadius, EngagementExitRadius);
	if (CurrentSlotComponent && CurrentSlotRequester.IsValid())
	{
		FVector ReservedLocation;
		EBHCombatSlotType ReservedSlotType = CurrentSlotType;
		int32 ReservedSlotIndex = CurrentSlotIndex;
		const bool bHasReservedLocation = CurrentSlotType == EBHCombatSlotType::Pending
			? CurrentSlotComponent->GetPendingWaitLocation(
				CurrentSlotRequester.Get(),
				ReservedSlotIndex,
				ReservedLocation)
			: CurrentSlotComponent->GetReservedSlot(
				CurrentSlotRequester.Get(),
				ReservedSlotType,
				ReservedSlotIndex,
				ReservedLocation);
		if (bHasReservedLocation)
		{
			EffectiveEngagementExitRadius = FMath::Max(
				EffectiveEngagementExitRadius,
				FVector::Dist2D(ReservedLocation, CurrentTarget->GetActorLocation())
					+ FMath::Max(0.0f, ReservedSlotExitMargin));
		}
	}
	if (bInEngagementFormation && DistanceToTarget >= EffectiveEngagementExitRadius)
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::LeftEngagementRange);
		bInEngagementFormation = false;
		ResetPursuitTracking();
	}
	else if (!bInEngagementFormation && DistanceToTarget <= FMath::Max(0.0f, EngagementEnterRadius))
	{
		bInEngagementFormation = true;
		// Preserve momentum while switching from Pursuit to Formation. The next
		// MoveTo request replaces the goal without a visible stop-start step.
		ResetPursuitTracking();
	}

	if (!bInEngagementFormation)
	{
		if (CurrentSlotType != EBHCombatSlotType::None)
		{
			ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::LeftEngagementRange);
		}
		ControlledEnemy->SetWantsRunLocomotion(true);
		ApplyMovementIntent(
			ControlledEnemy,
			PursuitSpeed,
			ControlledEnemy->CanMoveDuringAttack());
		if (UpdatePursuitStuckWatchdog(ControlledEnemy))
		{
			if (TryStartHighGroundDropRecovery(ControlledEnemy))
			{
				DrawDebugStatus(ControlledEnemy);
				return;
			}
			// Refresh both the path corridor and pursuit axis. The stable lane/row
			// identity remains unchanged; only the stale route is discarded.
			StopMovement();
			bHasRequestedPursuitMove = false;
			LastRequestedPursuitLocation = FVector::ZeroVector;
			SmoothedPursuitForward = FVector::ZeroVector;
		}
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
		const bool bHasInitialReservation = AcquireCombatSlot(ControlledEnemy);
		const bool bUseInitialRun = !bHasInitialReservation
			|| CurrentSlotType == EBHCombatSlotType::Attack;
		ControlledEnemy->SetWantsRunLocomotion(bUseInitialRun);
		ApplyMovementIntent(
			ControlledEnemy,
			bUseInitialRun ? PursuitSpeed : GetCurrentSlotMoveSpeed(ControlledEnemy),
			true);
		RequestPursuitMove(ControlledEnemy, InitialChargeStopRadius);
		TryStartMovingAttack(ControlledEnemy, DistanceToTarget);
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
	if (ResolvedRouteStage != EBHCombatMoveRouteStage::CoreEscape)
	{
		BH_DIAGNOSTICS(this,
			const bool bCleared = FVector::Dist2D(GetPawn()->GetActorLocation(), CurrentSlotComponent->GetOwner()->GetActorLocation())
				>= FMath::Max(CurrentSlotComponent->GetDiagnosticCoreExitRadius(), 0.0f);
			Diagnostics->EndCore(GetPawn(), bCleared, bCleared ? TEXT("ExitRadiusReached") : TEXT("RouteSuperseded")));
	}
	CurrentMoveRouteStage = ResolvedRouteStage;
	bEscapingCombatCore = CurrentMoveRouteStage == EBHCombatMoveRouteStage::CoreEscape;

	const float DistanceToSlotSquared = FVector::DistSquared2D(GetPawn()->GetActorLocation(), SlotLocation);
	const float DistanceToSlot = FMath::Sqrt(DistanceToSlotSquared);
	const float DistanceToMoveGoal = FVector::Dist2D(GetPawn()->GetActorLocation(), MoveGoal);
	LastDistanceToSlot = DistanceToSlot;
	// An intermediate route goal has priority over the final reservation. In
	// particular, a CoreEscape requester may be very close to a reformed Wait
	// slot while still inside the player core. Settling by final-slot distance at
	// that point would stop the escape forever and keep the old global promotion
	// gate active.
	const float EffectiveArrivalRadius = CurrentSlotType == EBHCombatSlotType::Wait
		? FMath::Max(SlotAcceptanceRadius, WaitSlotArrivalRadius)
		: SlotAcceptanceRadius;
	const bool bAtReservedSlot = !IsIntermediateCombatRouteStage(CurrentMoveRouteStage)
		&& DistanceToSlotSquared <= FMath::Square(FMath::Max(0.0f, EffectiveArrivalRadius));
	const float CurrentMoveSpeed = ControlledEnemy->GetVelocity().Size2D();
	const float EffectiveSettleSpeedThreshold = CurrentSlotType == EBHCombatSlotType::Wait
		? FMath::Max(SlotSettleSpeedThreshold, WaitSlotSettleSpeedThreshold)
		: SlotSettleSpeedThreshold;
	const bool bCanSettleAtReservedSlot = bAtReservedSlot
		&& CurrentMoveSpeed <= FMath::Max(0.0f, EffectiveSettleSpeedThreshold);
	if (!bAtReservedSlot)
	{
		switch (CurrentSlotType)
		{
		case EBHCombatSlotType::Attack:
			CurrentFormationMovementRole = EBHFormationMovementRole::AttackIngress;
			break;
		case EBHCombatSlotType::Wait:
			CurrentFormationMovementRole = EBHFormationMovementRole::WaitIngress;
			break;
		case EBHCombatSlotType::Holding:
			CurrentFormationMovementRole = EBHFormationMovementRole::HoldingTransit;
			break;
		case EBHCombatSlotType::Pending:
			CurrentFormationMovementRole = EBHFormationMovementRole::PendingTransit;
			break;
		case EBHCombatSlotType::None:
		default:
			break;
		}
	}
	else if (CurrentSlotType == EBHCombatSlotType::Holding)
	{
		CurrentFormationMovementRole = EBHFormationMovementRole::StationaryHolding;
	}
	if (CurrentSlotType == EBHCombatSlotType::Wait
		&& !bAtReservedSlot
		&& CurrentMoveRouteStage != EBHCombatMoveRouteStage::ExitDecompression)
	{
		// Departure priority is evaluated only before a new Wait movement leg.
		// An already moving Wait agent is never frozen in the middle of the choke.
		if (!bHasRequestedSlotMove || bWaitIngressDeferred)
		{
			AActor* BlockingAttackRequester = nullptr;
			if (CurrentSlotComponent->ShouldDeferWaitIngress(
				GetPawn(),
				MoveGoal,
				BlockingAttackRequester))
			{
				// Deferred Wait owners return before normal progress tracking below.
				// On the first retry, let an elevated owner jump directly to its
				// own Wait slot instead of remaining permanently outside the gate.
				if (bWaitIngressDeferred
					&& TryStartHighGroundDropRecovery(ControlledEnemy))
				{
					DrawDebugStatus(ControlledEnemy);
					return;
				}
				CurrentFormationMovementRole = EBHFormationMovementRole::WaitIngressDeferred;
				WaitIngressBlockingAttackRequester = BlockingAttackRequester;
				if (!bWaitIngressDeferred)
				{
					StopMovement();
				}
				bWaitIngressDeferred = true;
				bHasRequestedSlotMove = false;
				ResetStuckTracking();
				ControlledEnemy->SetFormationCatchUpRequired(false);
				ApplyMovementIntent(ControlledEnemy, WaitMoveSpeed, true);
				ScheduleWaitIngressRetry();
				DrawDebugStatus(ControlledEnemy);
				return;
			}
		}
		ClearWaitIngressDeferral();
	}
	else
	{
		ClearWaitIngressDeferral();
	}
	if (bAtReservedSlot)
	{
		// Arrival, not reservation, ends the initial approach run. Keep this
		// latched through later slot promotions and formation reforms.
		ControlledEnemy->MarkFormationJoined();
		ControlledEnemy->SetFormationCatchUpRequired(false);
	}
	else
	{
		UpdateFormationCatchUpIntent(ControlledEnemy, DistanceToSlot);
	}
	const bool bUseRunForCurrentSlot = CurrentSlotType == EBHCombatSlotType::Attack
		&& (ControlledEnemy->CanMoveDuringAttack()
			|| !ControlledEnemy->HasJoinedFormation()
			|| ControlledEnemy->NeedsFormationCatchUp());
	ControlledEnemy->SetWantsRunLocomotion(bUseRunForCurrentSlot);
	if (CurrentSlotType == EBHCombatSlotType::Attack && !bAtReservedSlot)
	{
		TryStartMovingAttack(ControlledEnemy, DistanceToTarget);
	}
	if (CurrentSlotType == EBHCombatSlotType::Attack && bAtReservedSlot)
	{
		ApplyMovementIntent(ControlledEnemy, AttackIngressSpeed, true);
		if (ControlledEnemy->CanMoveDuringAttack())
		{
			// Do not issue the old attack-lock StopMovement while a moving attack
			// naturally finishes its final approach to the slot.
			DrawDebugStatus(ControlledEnemy);
			return;
		}
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
		ApplyMovementIntent(ControlledEnemy, GetCurrentSlotMoveSpeed(ControlledEnemy), true);
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
		GetCurrentSlotMoveSpeed(ControlledEnemy),
		true);

	const bool bMovementStalled = !ControlledEnemy->IsAttackLocked()
		&& bHasRequestedSlotMove
		&& UpdateStuckTracking(DistanceToMoveGoal, ControlledEnemy->GetVelocity().Size2D());
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (bHasRequestedSlotMove
		&& NoProgressElapsed >= FMath::Max(0.1f, HighGroundDropNoProgressDelay)
		&& TryStartHighGroundDropRecovery(ControlledEnemy))
	{
		DrawDebugStatus(ControlledEnemy);
		return;
	}
	const bool bCanRequestHoldingYield = CurrentMoveRouteStage != EBHCombatMoveRouteStage::ExitDecompression
		&& (CurrentSlotType == EBHCombatSlotType::Attack
			|| CurrentSlotType == EBHCombatSlotType::Wait);
	bool bHoldingYieldStarted = false;
	if (bHasRequestedSlotMove
		&& bCanRequestHoldingYield
		&& NoProgressElapsed >= FMath::Max(0.1f, HoldingYieldTriggerDelay)
		&& CurrentTime >= HoldingYieldRequestCooldownUntil)
	{
		HoldingYieldRequestCooldownUntil = CurrentTime
			+ FMath::Max(0.0f, HoldingYieldRequestCooldown);
		bHoldingYieldStarted = CurrentSlotComponent->TryYieldBlockingStationaryHolding(
			ControlledEnemy,
			MoveGoal);
	}
	if (bHoldingYieldStarted)
	{
		bForceSlotPathRefresh = true;
		ResetStuckTracking();
	}
	else if (bMovementStalled)
	{
		StopMovement();
		RecoverStalledCombatSlot(ControlledEnemy);
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	const bool bRouteStageChanged = CurrentMoveRouteStage != LastRequestedRouteStage;
	const bool bSlotMoved = !bHasRequestedSlotMove
		|| FVector::DistSquared2D(LastRequestedSlotLocation, MoveGoal) >= FMath::Square(GetCurrentSlotRepathDistance());
	if (bTargetChanged
		|| bForceSlotPathRefresh
		|| bRouteStageChanged
		|| bSlotMoved
		|| GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		const bool bUsesRingWaypoint = CurrentMoveRouteStage == EBHCombatMoveRouteStage::ExitDecompression
			|| CurrentMoveRouteStage == EBHCombatMoveRouteStage::ApproachRing
			|| CurrentMoveRouteStage == EBHCombatMoveRouteStage::AlignOnRing
			|| CurrentMoveRouteStage == EBHCombatMoveRouteStage::BypassCorePositive
			|| CurrentMoveRouteStage == EBHCombatMoveRouteStage::BypassCoreNegative;
		RequestMoveToReservedSlot(
			MoveGoal,
			bUsesRingWaypoint
				? RingWaypointAcceptanceRadius
				: (CurrentSlotType == EBHCombatSlotType::Wait
					? FMath::Max(SlotAcceptanceRadius, WaitSlotArrivalRadius)
					: SlotAcceptanceRadius));
		TryStartMovingAttack(ControlledEnemy, DistanceToTarget);
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
		&& (IsHeroReachable(CurrentTarget)
			|| IsHeroHighGroundDropCandidate(CurrentTarget)))
	{
		return CurrentTarget.Get();
	}

	ABHHeroCharacter* ClosestReachableHero = nullptr;
	float ClosestReachableDistance = TNumericLimits<float>::Max();
	ABHHeroCharacter* ClosestDropCandidate = nullptr;
	float ClosestDropCandidateDistance = TNumericLimits<float>::Max();
	bool bCurrentTargetReachable = false;
	bool bCurrentTargetIsDropCandidate = false;
	float CurrentTargetDistance = TNumericLimits<float>::Max();

	for (TActorIterator<ABHHeroCharacter> It(GetWorld()); It; ++It)
	{
		ABHHeroCharacter* Hero = *It;
		if (!IsValid(Hero) || !Hero->IsPlayerControlled())
		{
			continue;
		}

		const bool bReachable = IsHeroReachable(Hero);
		const bool bDropCandidate = !bReachable
			&& IsHeroHighGroundDropCandidate(Hero);
		if (!bReachable && !bDropCandidate)
		{
			continue;
		}

		const float Distance = FVector::Dist2D(ControlledPawn->GetActorLocation(), Hero->GetActorLocation());
		if (Hero == CurrentTarget)
		{
			bCurrentTargetReachable = bReachable;
			bCurrentTargetIsDropCandidate = bDropCandidate;
			CurrentTargetDistance = Distance;
		}
		if (bReachable && Distance < ClosestReachableDistance)
		{
			ClosestReachableDistance = Distance;
			ClosestReachableHero = Hero;
		}
		else if (bDropCandidate && Distance < ClosestDropCandidateDistance)
		{
			ClosestDropCandidateDistance = Distance;
			ClosestDropCandidate = Hero;
		}
	}

	// Prefer a fully connected target once the current target hold expires.
	if (ClosestReachableHero)
	{
		if (!bCurrentTargetReachable || !IsValid(CurrentTarget))
		{
			return ClosestReachableHero;
		}

		if (CurrentTime - TargetAcquiredTime < MinimumTargetHoldTime
			|| ClosestReachableHero == CurrentTarget)
		{
			return CurrentTarget;
		}

		return ClosestReachableDistance + TargetSwitchDistanceAdvantage < CurrentTargetDistance
			? ClosestReachableHero
			: CurrentTarget.Get();
	}

	// Keep an unreachable lower hero only while a valid drop landing exists.
	if (!bCurrentTargetIsDropCandidate || !IsValid(CurrentTarget))
	{
		return ClosestDropCandidate;
	}
	if (CurrentTime - TargetAcquiredTime < MinimumTargetHoldTime
		|| ClosestDropCandidate == CurrentTarget)
	{
		return CurrentTarget;
	}

	return ClosestDropCandidate
		&& ClosestDropCandidateDistance + TargetSwitchDistanceAdvantage < CurrentTargetDistance
		? ClosestDropCandidate
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

	UNavigationPath* NavigationPath = FBHNavigationQuery::FindCompletePath(
		World,
		ControlledPawn->GetActorLocation(),
		ProjectedTargetLocation.Location,
		const_cast<APawn*>(ControlledPawn));
	return NavigationPath != nullptr;
}

bool ABHCrowdEnemyAIController::IsHeroHighGroundDropCandidate(
	const ABHHeroCharacter* Hero) const
{
	FVector LandingActorLocation;
	float DropHeight = 0.0f;
	float HorizontalDistance = 0.0f;
	return ResolveHighGroundDropLanding(
		Cast<ABHEnemy>(GetPawn()),
		Hero->GetActorLocation(),
		LandingActorLocation,
		DropHeight,
		HorizontalDistance);
}

bool ABHCrowdEnemyAIController::ResolveHighGroundDropLanding(
	const ABHEnemy* ControlledEnemy,
	const FVector& DesiredLandingGoal,
	FVector& OutLandingActorLocation,
	float& OutDropHeight,
	float& OutHorizontalDistance) const
{
	OutLandingActorLocation = FVector::ZeroVector;
	OutDropHeight = 0.0f;
	OutHorizontalDistance = 0.0f;

	UWorld* World = GetWorld();
	const UCharacterMovementComponent* Movement = ControlledEnemy
		? ControlledEnemy->GetCharacterMovement()
		: nullptr;
	if (!bEnableHighGroundDropRecovery
		|| !ControlledEnemy
		|| !Movement
		|| !Movement->IsMovingOnGround()
		|| ControlledEnemy->GetCombatState() != EBHEnemyCombatState::Chasing
		|| ControlledEnemy->IsAttackLocked()
		|| ControlledEnemy->IsHighGroundDropActive()
		|| !World)
	{
		return false;
	}

	const FVector StartLocation = ControlledEnemy->GetActorLocation();
	const float MinimumDropHeight = FMath::Max(0.0f, HighGroundDropMinimumHeight);
	if (StartLocation.Z - DesiredLandingGoal.Z < MinimumDropHeight)
	{
		return false;
	}

	const FVector TargetLocation = DesiredLandingGoal;
	FVector ToGoal = TargetLocation - StartLocation;
	ToGoal.Z = 0.0f;
	const float DesiredDistance = ToGoal.Size2D();
	const float MaximumDistance = FMath::Max(1.0f, HighGroundDropMaximumDistance);
	if (DesiredDistance <= UE_SMALL_NUMBER || DesiredDistance > MaximumDistance)
	{
		return false;
	}

	// Use the concrete formation goal instead of shortening every jump toward
	// the player center. Distinct Wait slots therefore produce distinct landings.
	const FVector DesiredLandingSurface = TargetLocation;

	const UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FNavLocation ProjectedLanding;
	if (!NavigationSystem
		|| !NavigationSystem->ProjectPointToNavigation(
			DesiredLandingSurface,
			ProjectedLanding,
			TargetNavProjectionExtent))
	{
		return false;
	}

	const UCapsuleComponent* Capsule = ControlledEnemy->GetCapsuleComponent();
	const float CapsuleHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f;
	const float CapsuleRadius = Capsule ? Capsule->GetScaledCapsuleRadius() : 0.0f;
	OutLandingActorLocation = ProjectedLanding.Location
		+ FVector(0.0f, 0.0f, CapsuleHalfHeight);
	OutDropHeight = StartLocation.Z - OutLandingActorLocation.Z;
	OutHorizontalDistance = FVector::Dist2D(StartLocation, OutLandingActorLocation);
	const float MinimumHorizontalDistance = FMath::Max(80.0f, CapsuleRadius * 2.0f);
	return OutDropHeight >= MinimumDropHeight
		&& OutDropHeight <= FMath::Max(MinimumDropHeight, HighGroundDropMaximumHeight)
		&& OutHorizontalDistance >= MinimumHorizontalDistance
		&& OutHorizontalDistance <= MaximumDistance + 1.0f;
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
		// Preserve watchdog history for the same logical reservation. The concrete
		// Move Goal comparison resets it later only if the destination truly moved.
		bForceSlotPathRefresh = true;
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
		bForceSlotPathRefresh = false;
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

	const FVector PredictedTargetLocation = CurrentTarget->GetActorLocation()
		+ CurrentTarget->GetVelocity() * FMath::Max(0.0f, PursuitPredictionTime);
	FVector PursuitGoal = PredictedTargetLocation;
	// Initial formation collection still uses pursuit movement. Keep those agents
	// on stable lane/row goals while the shared manager finalizes slot ownership,
	// otherwise every provisional member converges on the player actor.
	const bool bInitialFormationPursuit = bInEngagementFormation
		&& CurrentSlotComponent
		&& CurrentSlotComponent->IsInitialFormationPending();
	const bool bUseDistributedGoal = bEnableDistributedPursuitGoals
		&& (!bInEngagementFormation || bInitialFormationPursuit);
	if (bUseDistributedGoal)
	{
		const int32 ResolvedLaneCount = FMath::Max(1, PursuitLaneCount);
		const int32 ResolvedRowCount = FMath::Max(1, PursuitRowCount);
		ActivePursuitLaneIndex = static_cast<int32>(PursuitDistributionSeed % static_cast<uint32>(ResolvedLaneCount));
		ActivePursuitRowIndex = static_cast<int32>(
			(PursuitDistributionSeed / static_cast<uint32>(ResolvedLaneCount))
			% static_cast<uint32>(ResolvedRowCount));

		FVector DesiredForward = CurrentTarget->GetVelocity().GetSafeNormal2D();
		if (CurrentTarget->GetVelocity().SizeSquared2D()
			< FMath::Square(FMath::Max(0.0f, PursuitDirectionSpeedThreshold)))
		{
			// A stationary target has no meaningful travel axis. Preserve this
			// enemy's existing approach side instead of making the whole crowd
			// rotate to the player's facing direction.
			DesiredForward = (CurrentTarget->GetActorLocation() - ControlledEnemy->GetActorLocation()).GetSafeNormal2D();
		}
		if (DesiredForward.IsNearlyZero())
		{
			DesiredForward = ControlledEnemy->GetActorForwardVector().GetSafeNormal2D();
		}

		if (SmoothedPursuitForward.IsNearlyZero())
		{
			SmoothedPursuitForward = DesiredForward;
		}
		else
		{
			const float DirectionBlendAlpha = FMath::Clamp(PursuitDirectionBlendAlpha, 0.0f, 1.0f);
			const FVector BlendedForward = FMath::Lerp(
				SmoothedPursuitForward,
				DesiredForward,
				DirectionBlendAlpha).GetSafeNormal2D();
			SmoothedPursuitForward = BlendedForward.IsNearlyZero()
				? DesiredForward
				: BlendedForward;
		}

		const FVector PursuitRight(-SmoothedPursuitForward.Y, SmoothedPursuitForward.X, 0.0f);
		const float CenteredLane = static_cast<float>(ActivePursuitLaneIndex)
			- (static_cast<float>(ResolvedLaneCount) - 1.0f) * 0.5f;
		const float RowDistance = FMath::Max(0.0f, PursuitFirstRowDistance)
			+ static_cast<float>(ActivePursuitRowIndex) * FMath::Max(0.0f, PursuitRowSpacing);
		PursuitGoal += PursuitRight * CenteredLane * FMath::Max(0.0f, PursuitLaneSpacing)
			- SmoothedPursuitForward * RowDistance;
	}
	else
	{
		ActivePursuitLaneIndex = INDEX_NONE;
		ActivePursuitRowIndex = INDEX_NONE;
	}

	if (const UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation ProjectedGoal;
		if (NavigationSystem->ProjectPointToNavigation(PursuitGoal, ProjectedGoal, TargetNavProjectionExtent))
		{
			PursuitGoal = ProjectedGoal.Location;
		}
		else if (bUseDistributedGoal
			&& NavigationSystem->ProjectPointToNavigation(PredictedTargetLocation, ProjectedGoal, TargetNavProjectionExtent))
		{
			// A lane may temporarily fall outside the NavMesh beside a wall. Fall
			// back to the reachable center prediction rather than issuing a bad move.
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
	if (UCrowdFollowingComponent* CrowdFollowing = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		CrowdFollowing->SetCrowdSlowdownAtGoal(true);
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
		if (TryStartHighGroundDropRecovery(ControlledEnemy))
		{
			return;
		}
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

bool ABHCrowdEnemyAIController::TryStartMovingAttack(
	ABHEnemy* ControlledEnemy,
	float DistanceToTarget)
{
	if (!ControlledEnemy
		|| !IsValid(CurrentTarget)
		|| CurrentSlotType != EBHCombatSlotType::Attack
		|| bEscapingCombatCore
		|| CurrentMoveRouteStage == EBHCombatMoveRouteStage::ExitDecompression
		|| (CurrentSlotComponent
			&& CurrentSlotComponent->IsRequesterInExitDecompression(ControlledEnemy))
		|| ControlledEnemy->IsAttackLocked()
		|| !ControlledEnemy->IsMovingAttackEnabled()
		|| DistanceToTarget > ControlledEnemy->GetMovingAttackStartRange()
		|| GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		return false;
	}

	const bool bStarted = ControlledEnemy->TryStartBasicAttack(
		CurrentTarget,
		EBHEnemyAttackPresentationMode::MovingUpperBody);
	if (bStarted)
	{
		ControlledEnemy->SetWantsRunLocomotion(true);
		ApplyMovementIntent(ControlledEnemy, AttackIngressSpeed, true);
	}
	return bStarted;
}

float ABHCrowdEnemyAIController::GetCurrentSlotMoveSpeed(const ABHEnemy* ControlledEnemy) const
{
	if (CurrentSlotType == EBHCombatSlotType::Attack
		&& ControlledEnemy
		&& (!ControlledEnemy->HasJoinedFormation()
			|| ControlledEnemy->NeedsFormationCatchUp()))
	{
		return FormationCatchUpMoveSpeed;
	}

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

void ABHCrowdEnemyAIController::UpdateFormationCatchUpIntent(
	ABHEnemy* ControlledEnemy,
	float DistanceToSlot) const
{
	if (!ControlledEnemy)
	{
		return;
	}

	if (!ControlledEnemy->HasJoinedFormation())
	{
		ControlledEnemy->SetFormationCatchUpRequired(false);
		return;
	}
	if (CurrentSlotType != EBHCombatSlotType::Attack)
	{
		ControlledEnemy->SetFormationCatchUpRequired(false);
		return;
	}

	const float ExitDistance = FMath::Max(
		FMath::Max(0.0f, FormationCatchUpExitDistance),
		FMath::Max(0.0f, SlotAcceptanceRadius));
	const float EnterDistance = FMath::Max(
		ExitDistance,
		FMath::Max(0.0f, FormationCatchUpEnterDistance));

	if (ControlledEnemy->NeedsFormationCatchUp())
	{
		if (DistanceToSlot <= ExitDistance)
		{
			ControlledEnemy->SetFormationCatchUpRequired(false);
		}
	}
	else if (DistanceToSlot >= EnterDistance)
	{
		ControlledEnemy->SetFormationCatchUpRequired(true);
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

bool ABHCrowdEnemyAIController::UpdatePursuitStuckWatchdog(const ABHEnemy* ControlledEnemy)
{
	if (!bEnablePursuitStuckWatchdog || !ControlledEnemy || !GetWorld())
	{
		ResetPursuitStuckWatchdog();
		return false;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime < PursuitWatchdogCooldownUntil)
	{
		return false;
	}
	if (!bHasRequestedPursuitMove || GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		PursuitWatchdogReferenceLocation = ControlledEnemy->GetActorLocation();
		PursuitWatchdogNoProgressElapsed = 0.0f;
		bHasPursuitWatchdogSample = false;
		return false;
	}

	const FVector CurrentLocation = ControlledEnemy->GetActorLocation();
	if (!bHasPursuitWatchdogSample)
	{
		PursuitWatchdogReferenceLocation = CurrentLocation;
		PursuitWatchdogNoProgressElapsed = 0.0f;
		bHasPursuitWatchdogSample = true;
		return false;
	}

	if (FVector::DistSquared2D(PursuitWatchdogReferenceLocation, CurrentLocation)
		>= FMath::Square(FMath::Max(1.0f, PursuitWatchdogProgressDistance)))
	{
		PursuitWatchdogReferenceLocation = CurrentLocation;
		PursuitWatchdogNoProgressElapsed = 0.0f;
		return false;
	}

	PursuitWatchdogNoProgressElapsed += ResolvedTargetRefreshInterval;
	if (PursuitWatchdogNoProgressElapsed < FMath::Max(0.1f, PursuitStuckWatchdogTimeout))
	{
		return false;
	}

	PursuitWatchdogReferenceLocation = CurrentLocation;
	PursuitWatchdogNoProgressElapsed = 0.0f;
	bHasPursuitWatchdogSample = false;
	PursuitWatchdogCooldownUntil = CurrentTime + FMath::Max(0.0f, PursuitWatchdogCooldown);
	++PursuitWatchdogRecoveryCount;
	UE_LOG(
		LogProjectBH,
		Verbose,
		TEXT("%s pursuit watchdog is refreshing a no-progress route (recovery %d)."),
		*GetName(),
		PursuitWatchdogRecoveryCount);
	return true;
}

void ABHCrowdEnemyAIController::ResetPursuitStuckWatchdog()
{
	PursuitWatchdogReferenceLocation = FVector::ZeroVector;
	PursuitWatchdogNoProgressElapsed = 0.0f;
	PursuitWatchdogCooldownUntil = 0.0f;
	bHasPursuitWatchdogSample = false;
}

void ABHCrowdEnemyAIController::ResetPursuitTracking()
{
	bHasRequestedPursuitMove = false;
	LastRequestedPursuitLocation = FVector::ZeroVector;
	SmoothedPursuitForward = FVector::ZeroVector;
	ActivePursuitLaneIndex = INDEX_NONE;
	ActivePursuitRowIndex = INDEX_NONE;
	ResetPursuitStuckWatchdog();
}

bool ABHCrowdEnemyAIController::TryStartHighGroundDropRecovery(
	ABHEnemy* ControlledEnemy)
{
	UWorld* World = GetWorld();
	if (!bEnableHighGroundDropRecovery
		|| !ControlledEnemy
		|| !IsValid(CurrentTarget)
		|| CurrentSlotType != EBHCombatSlotType::Wait
		|| !IsValid(CurrentSlotComponent)
		|| !CurrentSlotRequester.IsValid()
		|| !World
		|| World->GetTimeSeconds() < HighGroundDropCooldownUntil)
	{
		return false;
	}

	EBHCombatSlotType ReservedSlotType = CurrentSlotType;
	int32 ReservedSlotIndex = CurrentSlotIndex;
	FVector WaitSlotLocation;
	if (!CurrentSlotComponent->GetReservedSlot(
		CurrentSlotRequester.Get(),
		ReservedSlotType,
		ReservedSlotIndex,
		WaitSlotLocation)
		|| ReservedSlotType != EBHCombatSlotType::Wait
		|| ReservedSlotIndex == INDEX_NONE)
	{
		return false;
	}

	FVector LandingActorLocation;
	float DropHeight = 0.0f;
	float HorizontalDistance = 0.0f;
	if (!ResolveHighGroundDropLanding(
		ControlledEnemy,
		WaitSlotLocation,
		LandingActorLocation,
		DropHeight,
		HorizontalDistance))
	{
		return false;
	}

	if (!ControlledEnemy->TryStartHighGroundDrop(
		LandingActorLocation,
		HighGroundDropLaunchZ,
		HighGroundDropMaximumHorizontalSpeed))
	{
		return false;
	}

	// The character validates first and atomically stops path following as it
	// commits the launch. Only discard controller tracking after that succeeds.
	bHasRequestedPursuitMove = false;
	bHasRequestedSlotMove = false;
	bForceSlotPathRefresh = true;
	ClearWaitIngressDeferral();
	ResetPursuitStuckWatchdog();
	ResetStuckTracking();

	HighGroundDropCooldownUntil = World->GetTimeSeconds()
		+ FMath::Max(0.0f, HighGroundDropCooldown);
	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s started a high-ground drop recovery toward Wait slot %d: %.0f cm down, %.0f cm forward."),
		*ControlledEnemy->GetName(),
		ReservedSlotIndex,
		DropHeight,
		HorizontalDistance);
	return true;
}

void ABHCrowdEnemyAIController::ReleaseCurrentCombatSlot(
	EBHCombatSlotReleaseReason Reason,
	bool bTemporarilyExcludeReleasedSlot)
{
	FinishTemporaryHoldingYield(true);

	if (Reason == EBHCombatSlotReleaseReason::TargetChanged
		|| Reason == EBHCombatSlotReleaseReason::TargetLost
		|| Reason == EBHCombatSlotReleaseReason::LeftEngagementRange
		|| Reason == EBHCombatSlotReleaseReason::UnPossessed
		|| Reason == EBHCombatSlotReleaseReason::Died)
	{
		if (ABHEnemy* ControlledEnemy = Cast<ABHEnemy>(GetPawn()))
		{
			ControlledEnemy->ResetFormationJoinState();
		}
	}

	const bool bHadReservation = CurrentSlotType != EBHCombatSlotType::None && CurrentSlotIndex != INDEX_NONE;
	BH_DIAGNOSTICS(this,
		if (bHadReservation) Diagnostics->Release(GetPawn(), CurrentSlotComponent, CurrentSlotIndex,
			CurrentSlotComponent ? CurrentSlotComponent->GetFormationRevision() : INDEX_NONE,
			StaticEnum<EBHCombatSlotReleaseReason>()->GetNameStringByValue(static_cast<int64>(Reason)));
		else Diagnostics->EndCore(GetPawn(), false, TEXT("ReservationReleased")));
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
	bEscapingCombatCore = false;
	CurrentFormationMovementRole = EBHFormationMovementRole::None;
	ClearWaitIngressDeferral();
	LastRequestedSlotLocation = FVector::ZeroVector;
	bHasRequestedSlotMove = false;
	bForceSlotPathRefresh = false;
	TrackedSlotType = EBHCombatSlotType::None;
	TrackedSlotIndex = INDEX_NONE;
	ResetStuckTracking();
}

void ABHCrowdEnemyAIController::RecoverStalledCombatSlot(ABHEnemy* ControlledEnemy)
{
	if (!ControlledEnemy || !IsValid(CurrentSlotComponent))
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::Stalled, true);
		return;
	}

	if (CurrentSlotType == EBHCombatSlotType::Attack
		&& !bEscapingCombatCore
		&& CurrentSlotComponent->HandleStalledAttackReservation(
			ControlledEnemy,
			FailedSlotCooldown))
	{
		BH_DIAGNOSTICS(this, Diagnostics->Release(GetPawn(), CurrentSlotComponent, CurrentSlotIndex, CurrentSlotComponent->GetFormationRevision(), TEXT("Stalled")));
		LastReleaseReason = EBHCombatSlotReleaseReason::Stalled;
		bHasRequestedSlotMove = false;
		CurrentMoveRouteStage = EBHCombatMoveRouteStage::Direct;
		LastRequestedRouteStage = EBHCombatMoveRouteStage::Direct;
		ResetStuckTracking();

		FVector DemotedSlotLocation;
		if (CurrentSlotComponent->GetReservedSlot(
			ControlledEnemy,
			CurrentSlotType,
			CurrentSlotIndex,
			DemotedSlotLocation))
		{
			LastDistanceToSlot = FVector::Dist2D(
				ControlledEnemy->GetActorLocation(),
				DemotedSlotLocation);
		}
		TrackedSlotType = CurrentSlotType;
		TrackedSlotIndex = CurrentSlotIndex;
		return;
	}

	ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::Stalled, true);
}

bool ABHCrowdEnemyAIController::TryStartOverlapRecovery(ABHEnemy* ControlledEnemy)
{
	if (!bEnableOverlapRecovery
		|| !ControlledEnemy
		|| !GetWorld()
		|| ControlledEnemy->IsAttackLocked()
		|| !ControlledEnemy->IsPoolActive()
		|| GetWorld()->GetTimeSeconds() < OverlapRecoveryCooldownUntil)
	{
		return false;
	}

	FVector EscapeGoal;
	ABHEnemy* PrimaryBlocker = nullptr;
	if (!FindOverlapEscapeGoal(ControlledEnemy, EscapeGoal, PrimaryBlocker))
	{
		return false;
	}

	// A real overlap on an upper route is already sufficient congestion evidence.
	// Prefer leaving the crowded level over repeatedly shuffling sideways there.
	if (TryStartHighGroundDropRecovery(ControlledEnemy))
	{
		return true;
	}

	// Abort the old path before marking the escape request active. Its aborted
	// completion must still be interpreted as the old pursuit/slot request.
	StopMovement();
	bHasRequestedPursuitMove = false;
	bHasRequestedSlotMove = false;

	bOverlapRecoveryActive = true;
	OverlapRecoveryGoal = EscapeGoal;
	OverlapRecoveryBlocker = PrimaryBlocker;
	OverlapRecoveryEndTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.1f, OverlapEscapeDuration);
	OverlapRecoveryRequestID = FAIRequestID::InvalidRequest;
	ResetStuckTracking();

	// The normal crowd solver cannot break a perfectly symmetric overlap: both
	// agents keep predicting the other into the same correction. Only the yielding
	// agent temporarily ignores crowd separation while NavMesh/path collision still
	// keeps the short escape move inside the traversable world.
	if (UCrowdFollowingComponent* CrowdFollowing = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		CrowdFollowing->SetCrowdObstacleAvoidance(false);
		CrowdFollowing->SetCrowdSeparation(false);
		CrowdFollowing->SetCrowdSlowdownAtGoal(false);
	}

	FAIMoveRequest EscapeRequest(EscapeGoal);
	EscapeRequest.SetAcceptanceRadius(10.0f);
	EscapeRequest.SetReachTestIncludesAgentRadius(false);
	EscapeRequest.SetReachTestIncludesGoalRadius(false);
	EscapeRequest.SetUsePathfinding(true);
	EscapeRequest.SetAllowPartialPath(false);
	EscapeRequest.SetProjectGoalLocation(false);
	EscapeRequest.SetCanStrafe(true);
	const FPathFollowingRequestResult RequestResult = MoveTo(EscapeRequest);
	if (RequestResult.Code == EPathFollowingRequestResult::Failed
		|| RequestResult.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		FinishOverlapRecovery(false);
		return false;
	}

	OverlapRecoveryRequestID = RequestResult.MoveId;
	UE_LOG(
		LogProjectBH,
		Verbose,
		TEXT("%s started overlap escape from %s toward %s without releasing its combat slot."),
		*GetName(),
		IsValid(PrimaryBlocker) ? *PrimaryBlocker->GetName() : TEXT("overlap cluster"),
		*EscapeGoal.ToCompactString());
	return true;
}

bool ABHCrowdEnemyAIController::UpdateOverlapRecovery(ABHEnemy* ControlledEnemy)
{
	if (!bOverlapRecoveryActive || !ControlledEnemy || !GetWorld())
	{
		return false;
	}

	bool bStillYieldingToOverlap = false;
	for (TActorIterator<ABHEnemy> It(GetWorld()); It; ++It)
	{
		ABHEnemy* OtherEnemy = *It;
		if (IsEnemyOverlapping(ControlledEnemy, OtherEnemy)
			&& ShouldYieldOverlap(ControlledEnemy, OtherEnemy))
		{
			bStillYieldingToOverlap = true;
			break;
		}
	}

	const bool bReachedEscapeGoal = FVector::DistSquared2D(
		ControlledEnemy->GetActorLocation(),
		OverlapRecoveryGoal) <= FMath::Square(15.0f);
	const bool bTimedOut = GetWorld()->GetTimeSeconds() >= OverlapRecoveryEndTime;
	if (!bStillYieldingToOverlap || bReachedEscapeGoal || bTimedOut)
	{
		FinishOverlapRecovery(!bReachedEscapeGoal);
		return false;
	}

	return true;
}

void ABHCrowdEnemyAIController::FinishOverlapRecovery(bool bStopEscapeMove)
{
	if (!bOverlapRecoveryActive)
	{
		return;
	}

	// Clear first so an Aborted completion emitted by StopMovement cannot be
	// mistaken for the escape request itself.
	bOverlapRecoveryActive = false;
	OverlapRecoveryRequestID = FAIRequestID::InvalidRequest;
	if (bStopEscapeMove)
	{
		StopMovement();
	}

	OverlapRecoveryGoal = FVector::ZeroVector;
	OverlapRecoveryBlocker.Reset();
	OverlapRecoveryEndTime = 0.0f;
	OverlapRecoveryCooldownUntil = GetWorld()
		? GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, OverlapRecoveryCooldown)
		: 0.0f;
	ApplyCrowdFollowingSettings();
	bForceSlotPathRefresh = true;
	ResetStuckTracking();
}

bool ABHCrowdEnemyAIController::FindOverlapEscapeGoal(
	ABHEnemy* ControlledEnemy,
	FVector& OutEscapeGoal,
	ABHEnemy*& OutPrimaryBlocker) const
{
	OutEscapeGoal = FVector::ZeroVector;
	OutPrimaryBlocker = nullptr;
	if (!ControlledEnemy || !GetWorld())
	{
		return false;
	}

	const UCapsuleComponent* ControlledCapsule = ControlledEnemy->GetCapsuleComponent();
	if (!ControlledCapsule)
	{
		return false;
	}

	const FVector ControlledLocation = ControlledEnemy->GetActorLocation();
	FVector CombinedAway = FVector::ZeroVector;
	float DeepestPenetration = 0.0f;
	TArray<TObjectPtr<ABHEnemy>> YieldBlockers;
	for (TActorIterator<ABHEnemy> It(GetWorld()); It; ++It)
	{
		ABHEnemy* OtherEnemy = *It;
		if (!IsEnemyOverlapping(ControlledEnemy, OtherEnemy)
			|| !ShouldYieldOverlap(ControlledEnemy, OtherEnemy))
		{
			continue;
		}

		YieldBlockers.Add(OtherEnemy);
		const UCapsuleComponent* OtherCapsule = OtherEnemy->GetCapsuleComponent();
		const FVector Difference = ControlledLocation - OtherEnemy->GetActorLocation();
		const float Distance = Difference.Size2D();
		const float DesiredClearance = ControlledCapsule->GetScaledCapsuleRadius()
			+ (OtherCapsule ? OtherCapsule->GetScaledCapsuleRadius() : 0.0f)
			+ FMath::Max(0.0f, OverlapEscapePadding);
		const float Penetration = FMath::Max(1.0f, DesiredClearance - Distance);
		if (Distance > KINDA_SMALL_NUMBER)
		{
			CombinedAway += Difference.GetSafeNormal2D() * Penetration;
		}
		if (Penetration > DeepestPenetration)
		{
			DeepestPenetration = Penetration;
			OutPrimaryBlocker = OtherEnemy;
		}
	}

	if (YieldBlockers.IsEmpty())
	{
		return false;
	}

	if (CombinedAway.IsNearlyZero())
	{
		// Golden-angle hashing scatters actors spawned at the exact same transform,
		// while remaining deterministic enough that they do not all choose one side.
		const float AngleDegrees = FMath::Fmod(
			static_cast<float>(ControlledEnemy->GetUniqueID()) * 137.507764f,
			360.0f);
		CombinedAway = FVector(
			FMath::Cos(FMath::DegreesToRadians(AngleDegrees)),
			FMath::Sin(FMath::DegreesToRadians(AngleDegrees)),
			0.0f);
	}
	CombinedAway = CombinedAway.GetSafeNormal2D();

	const UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavigationSystem)
	{
		return false;
	}

	struct FOverlapEscapeCandidate
	{
		FVector Location = FVector::ZeroVector;
		float Score = -TNumericLimits<float>::Max();
	};
	TArray<FOverlapEscapeCandidate> Candidates;
	const float EscapeDistance = FMath::Max(
		FMath::Max(10.0f, OverlapEscapeDistance),
		DeepestPenetration);
	const bool bReverseSweep = (ControlledEnemy->GetUniqueID() & 1u) != 0;
	const float CandidateAngles[] = { 0.0f, 45.0f, -45.0f, 90.0f, -90.0f, 135.0f, -135.0f, 180.0f };
	for (float CandidateAngle : CandidateAngles)
	{
		const float ResolvedAngle = bReverseSweep ? -CandidateAngle : CandidateAngle;
		const FVector CandidateDirection = CombinedAway.RotateAngleAxis(ResolvedAngle, FVector::UpVector);
		FNavLocation ProjectedLocation;
		if (!NavigationSystem->ProjectPointToNavigation(
			ControlledLocation + CandidateDirection * EscapeDistance,
			ProjectedLocation,
			TargetNavProjectionExtent)
			|| FVector::DistSquared2D(ControlledLocation, ProjectedLocation.Location) < FMath::Square(25.0f))
		{
			continue;
		}

		float MinimumEnemyClearance = TNumericLimits<float>::Max();
		for (TActorIterator<ABHEnemy> OtherIt(GetWorld()); OtherIt; ++OtherIt)
		{
			const ABHEnemy* OtherEnemy = *OtherIt;
			if (OtherEnemy == ControlledEnemy || !OtherEnemy->IsPoolActive())
			{
				continue;
			}
			MinimumEnemyClearance = FMath::Min(
				MinimumEnemyClearance,
				FVector::Dist2D(ProjectedLocation.Location, OtherEnemy->GetActorLocation()));
		}
		if (MinimumEnemyClearance == TNumericLimits<float>::Max())
		{
			MinimumEnemyClearance = EscapeDistance;
		}

		const FVector ActualDirection = (ProjectedLocation.Location - ControlledLocation).GetSafeNormal2D();
		FOverlapEscapeCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Location = ProjectedLocation.Location;
		Candidate.Score = MinimumEnemyClearance
			+ FVector::DotProduct(ActualDirection, CombinedAway) * 50.0f;
	}

	Candidates.Sort([](const FOverlapEscapeCandidate& Left, const FOverlapEscapeCandidate& Right)
	{
		return Left.Score > Right.Score;
	});
	for (const FOverlapEscapeCandidate& Candidate : Candidates)
	{
		UNavigationPath* EscapePath = FBHNavigationQuery::FindCompletePath(
			GetWorld(),
			ControlledLocation,
			Candidate.Location,
			ControlledEnemy);
		if (EscapePath)
		{
			OutEscapeGoal = Candidate.Location;
			return true;
		}
	}

	return false;
}

bool ABHCrowdEnemyAIController::IsEnemyOverlapping(
	const ABHEnemy* FirstEnemy,
	const ABHEnemy* SecondEnemy) const
{
	if (!IsValid(FirstEnemy)
		|| !IsValid(SecondEnemy)
		|| FirstEnemy == SecondEnemy
		|| !FirstEnemy->IsPoolActive()
		|| !SecondEnemy->IsPoolActive())
	{
		return false;
	}

	const UCapsuleComponent* FirstCapsule = FirstEnemy->GetCapsuleComponent();
	const UCapsuleComponent* SecondCapsule = SecondEnemy->GetCapsuleComponent();
	if (!FirstCapsule || !SecondCapsule)
	{
		return false;
	}

	const FVector FirstLocation = FirstEnemy->GetActorLocation();
	const FVector SecondLocation = SecondEnemy->GetActorLocation();
	if (FMath::Abs(FirstLocation.Z - SecondLocation.Z) > FMath::Max(0.0f, OverlapHeightTolerance))
	{
		return false;
	}

	const float DetectionDistance = (
		FirstCapsule->GetScaledCapsuleRadius()
		+ SecondCapsule->GetScaledCapsuleRadius())
		* FMath::Clamp(OverlapDetectionScale, 0.1f, 1.5f);
	return FVector::DistSquared2D(FirstLocation, SecondLocation)
		< FMath::Square(DetectionDistance);
}

bool ABHCrowdEnemyAIController::ShouldYieldOverlap(
	const ABHEnemy* ControlledEnemy,
	const ABHEnemy* OtherEnemy) const
{
	const int32 ControlledPriority = GetOverlapPriority(ControlledEnemy);
	const int32 OtherPriority = GetOverlapPriority(OtherEnemy);
	if (ControlledPriority != OtherPriority)
	{
		return ControlledPriority < OtherPriority;
	}

	// Stable tie-breaker: exactly one member of a pair yields, so two equal agents
	// cannot mirror each other's recovery and remain deadlocked.
	return ControlledEnemy && OtherEnemy
		&& ControlledEnemy->GetUniqueID() > OtherEnemy->GetUniqueID();
}

int32 ABHCrowdEnemyAIController::GetOverlapPriority(const ABHEnemy* Enemy) const
{
	if (!IsValid(Enemy))
	{
		return 0;
	}
	const int32 SizePriority = Enemy->GetEnemySizeClass() == EBHEnemySizeClass::Large
		? 1000
		: 0;

	switch (Enemy->GetCombatState())
	{
	case EBHEnemyCombatState::Staggered:
		return SizePriority + 600;
	case EBHEnemyCombatState::Attacking:
		return SizePriority + 550;
	case EBHEnemyCombatState::Recovering:
		return SizePriority + 500;
	case EBHEnemyCombatState::Dead:
		return 0;
	case EBHEnemyCombatState::Chasing:
	default:
		break;
	}

	const ABHCrowdEnemyAIController* OtherController = Cast<ABHCrowdEnemyAIController>(Enemy->GetController());
	if (!OtherController)
	{
		// A live enemy without this controller cannot perform its own escape, so the
		// controlled crowd agent must route around it.
		return SizePriority + 450;
	}

	switch (OtherController->GetCurrentCombatSlotType())
	{
	case EBHCombatSlotType::Attack:
		return SizePriority + 400;
	case EBHCombatSlotType::Wait:
		return SizePriority + 300;
	case EBHCombatSlotType::Holding:
		return SizePriority + 250;
	case EBHCombatSlotType::Pending:
		return SizePriority + 200;
	case EBHCombatSlotType::None:
	default:
		return SizePriority + 100;
	}
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

void ABHCrowdEnemyAIController::ScheduleWaitIngressRetry()
{
	if (!GetWorld()
		|| GetWorldTimerManager().IsTimerActive(WaitIngressRetryTimerHandle))
	{
		return;
	}

	GetWorldTimerManager().SetTimer(
		WaitIngressRetryTimerHandle,
		this,
		&ThisClass::RefreshTargetAndMove,
		FMath::Max(0.05f, WaitIngressDeferRetryInterval),
		false);
}

void ABHCrowdEnemyAIController::ClearWaitIngressDeferral()
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(WaitIngressRetryTimerHandle);
	}
	bWaitIngressDeferred = false;
	WaitIngressBlockingAttackRequester.Reset();
}

bool ABHCrowdEnemyAIController::UpdateTemporaryHoldingYield(ABHEnemy* ControlledEnemy)
{
	if (!bHoldingYieldActive || !ControlledEnemy || !GetWorld())
	{
		return false;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	const float Elapsed = CurrentTime - HoldingYieldStartTime;
	AActor* PassingRequester = HoldingYieldPassingRequester.Get();
	const float PassedDistance = PassingRequester
		? FVector::DotProduct(
			PassingRequester->GetActorLocation() - HoldingYieldOrigin,
			HoldingYieldPassingDirection)
		: 0.0f;
	const bool bPassed = bHoldingYieldAtGoal
		&& PassingRequester
		&& Elapsed >= FMath::Max(0.0f, HoldingYieldMinimumDuration)
		&& PassedDistance >= FMath::Max(0.0f, HoldingYieldPassDistance);
	const bool bTimedOut = Elapsed >= FMath::Max(
		FMath::Max(0.1f, HoldingYieldMinimumDuration),
		HoldingYieldMaximumDuration);
	if (!PassingRequester
		|| CurrentSlotType != EBHCombatSlotType::Holding
		|| !IsValid(CurrentSlotComponent)
		|| bPassed
		|| bTimedOut)
	{
		FinishTemporaryHoldingYield(true);
		return false;
	}

	if (!bHoldingYieldAtGoal
		&& HoldingYieldRequestID == FAIRequestID::InvalidRequest
		&& GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		FinishTemporaryHoldingYield(false);
		return false;
	}

	CurrentFormationMovementRole = EBHFormationMovementRole::HoldingYield;
	ControlledEnemy->SetWantsRunLocomotion(false);
	ControlledEnemy->SetFormationCatchUpRequired(false);
	ApplyMovementIntent(ControlledEnemy, HoldingMoveSpeed, true);
	return true;
}

void ABHCrowdEnemyAIController::FinishTemporaryHoldingYield(bool bStopYieldMove)
{
	if (!bHoldingYieldActive)
	{
		return;
	}

	// Clear first so StopMovement's aborted completion cannot be interpreted as
	// the active temporary-yield request.
	bHoldingYieldActive = false;
	HoldingYieldRequestID = FAIRequestID::InvalidRequest;
	if (bStopYieldMove)
	{
		StopMovement();
	}

	bHoldingYieldAtGoal = false;
	HoldingYieldGoal = FVector::ZeroVector;
	HoldingYieldOrigin = FVector::ZeroVector;
	HoldingYieldPassingDirection = FVector::ZeroVector;
	HoldingYieldPassingRequester.Reset();
	HoldingYieldStartTime = 0.0f;
	HoldingYieldRequestCooldownUntil = GetWorld()
		? GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, HoldingYieldRequestCooldown)
		: 0.0f;
	CurrentFormationMovementRole = EBHFormationMovementRole::None;
	bHasRequestedSlotMove = false;
	bForceSlotPathRefresh = true;
	ResetStuckTracking();
}

void ABHCrowdEnemyAIController::RequestMoveToReservedSlot(
	const FVector& SlotLocation,
	float AcceptanceRadius)
{
	bForceSlotPathRefresh = false;
	const bool bMoveGoalChanged = !bHasRequestedSlotMove
		|| CurrentMoveRouteStage != LastRequestedRouteStage
		|| FVector::DistSquared2D(LastRequestedSlotLocation, SlotLocation)
			>= FMath::Square(GetCurrentSlotRepathDistance());
	if (bMoveGoalChanged)
	{
		// Progress samples only describe one concrete movement leg. Carrying them
		// across a changed slot/waypoint can falsely classify a valid repath as stuck.
		ResetStuckTracking();
	}

	if (UCrowdFollowingComponent* CrowdFollowing = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		CrowdFollowing->SetCrowdSlowdownAtGoal(!IsIntermediateCombatRouteStage(CurrentMoveRouteStage));
	}

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
		if (TryStartHighGroundDropRecovery(Cast<ABHEnemy>(GetPawn())))
		{
			return;
		}
		if (CurrentSlotType == EBHCombatSlotType::Attack)
		{
			RecoverStalledCombatSlot(Cast<ABHEnemy>(GetPawn()));
		}
		else
		{
			ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::MoveRequestFailed, true);
		}
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
		NoProgressElapsed = 0.0f;
		return false;
	}

	if (ProgressReferenceDistance - DistanceToSlot >= StuckProgressDistance)
	{
		ProgressReferenceDistance = DistanceToSlot;
		StuckElapsed = 0.0f;
		NoProgressElapsed = 0.0f;
		return false;
	}

	// Crowd collision can produce visible velocity while two agents merely push
	// against each other. Raw speed must not erase evidence that neither agent is
	// making net progress toward its actual movement goal.
	NoProgressElapsed += ResolvedTargetRefreshInterval;

	if (Speed < StuckSpeedThreshold)
	{
		StuckElapsed += ResolvedTargetRefreshInterval;
	}
	else
	{
		StuckElapsed = 0.0f;
	}

	const ABHEnemy* ControlledEnemy = Cast<ABHEnemy>(GetPawn());
	const float EffectiveNoProgressTimeout = CurrentSlotType == EBHCombatSlotType::Attack
		? AttackNoProgressTimeout
		: (ControlledEnemy && ControlledEnemy->WantsRunLocomotion()
			? RunNoProgressTimeout
			: NoProgressTimeout);
	return StuckElapsed >= StuckTimeout
		|| NoProgressElapsed >= FMath::Max(0.1f, EffectiveNoProgressTimeout);
}

void ABHCrowdEnemyAIController::ResetStuckTracking()
{
	ProgressReferenceDistance = 0.0f;
	StuckElapsed = 0.0f;
	NoProgressElapsed = 0.0f;
	LastDistanceToSlot = 0.0f;
	bHasProgressSample = false;
}

void ABHCrowdEnemyAIController::DrawDebugStatus(const ABHEnemy* ControlledEnemy) const
{
	if (!BHDebugDraw::IsCrowdEnabled(bDrawCrowdDebug) || !ControlledEnemy || !GetWorld())
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
	const FString TrafficRoleName = StaticEnum<EBHFormationMovementRole>()->GetNameStringByValue(
		static_cast<int64>(CurrentFormationMovementRole));
	const FString AttackModeName = StaticEnum<EBHEnemyAttackPresentationMode>()->GetNameStringByValue(
		static_cast<int64>(ControlledEnemy->GetAttackPresentationMode()));
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	const float TargetHeldTime = IsValid(CurrentTarget)
		? FMath::Max(0.0f, CurrentTime - TargetAcquiredTime)
		: 0.0f;
	const uint64 QueueSequence = CurrentSlotComponent && CurrentSlotRequester.IsValid()
		? CurrentSlotComponent->GetQueueSequenceForRequester(CurrentSlotRequester.Get())
		: 0;
	const int32 CorridorLane = CurrentSlotComponent && CurrentSlotRequester.IsValid()
		? CurrentSlotComponent->GetCorridorLaneForRequester(CurrentSlotRequester.Get())
		: INDEX_NONE;
	const int32 CorridorSide = CurrentSlotComponent && CurrentSlotRequester.IsValid()
		? CurrentSlotComponent->GetCorridorSideForRequester(CurrentSlotRequester.Get())
		: INDEX_NONE;
	FColor RouteColor = FColor::White;
	switch (CurrentMoveRouteStage)
	{
	case EBHCombatMoveRouteStage::ExitDecompression:
		RouteColor = FColor(80, 255, 160);
		break;
	case EBHCombatMoveRouteStage::ApproachRing:
		RouteColor = FColor::Cyan;
		break;
	case EBHCombatMoveRouteStage::AlignOnRing:
		RouteColor = FColor::Blue;
		break;
	case EBHCombatMoveRouteStage::Ingress:
		RouteColor = FColor::Orange;
		break;
	case EBHCombatMoveRouteStage::BypassCorePositive:
		RouteColor = FColor::Magenta;
		break;
	case EBHCombatMoveRouteStage::BypassCoreNegative:
		RouteColor = FColor(160, 80, 255);
		break;
	case EBHCombatMoveRouteStage::CoreEscape:
		RouteColor = FColor(0, 255, 128);
		break;
	case EBHCombatMoveRouteStage::Direct:
	default:
		break;
	}
	if (bOverlapRecoveryActive)
	{
		DrawDebugLine(
			GetWorld(),
			ControlledEnemy->GetActorLocation(),
			OverlapRecoveryGoal,
			FColor::Orange,
			false,
			ResolvedTargetRefreshInterval + 0.1f,
			0,
			4.0f);
		DrawDebugSphere(
			GetWorld(),
			OverlapRecoveryGoal,
			14.0f,
			8,
			FColor::Orange,
			false,
			ResolvedTargetRefreshInterval + 0.1f,
			0,
			2.0f);
	}
	else if (bHoldingYieldActive)
	{
		DrawDebugLine(
			GetWorld(),
			ControlledEnemy->GetActorLocation(),
			HoldingYieldGoal,
			FColor(180, 70, 255),
			false,
			ResolvedTargetRefreshInterval + 0.1f,
			0,
			4.0f);
		DrawDebugSphere(
			GetWorld(),
			HoldingYieldGoal,
			14.0f,
			8,
			FColor(180, 70, 255),
			false,
			ResolvedTargetRefreshInterval + 0.1f,
			0,
			2.0f);
	}
	else if (bWaitIngressDeferred && WaitIngressBlockingAttackRequester.IsValid())
	{
		DrawDebugLine(
			GetWorld(),
			ControlledEnemy->GetActorLocation(),
			WaitIngressBlockingAttackRequester->GetActorLocation(),
			FColor(255, 170, 0),
			false,
			ResolvedTargetRefreshInterval + 0.1f,
			0,
			3.0f);
	}
	else if (bHasRequestedSlotMove)
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
	const TCHAR* GaitIntent = ControlledEnemy->WantsRunLocomotion()
		? (ControlledEnemy->NeedsFormationCatchUp() ? TEXT("CatchUpRun") : TEXT("ApproachRun"))
		: TEXT("Formation");
	const FString DebugText = FString::Printf(
		TEXT("%s/%s %.2fs | Target:%s Held:%.1f | %s[%d] Side:%d Lane:%d Seq:%llu Dist:%.0f Speed:%.1f Gait:%s Facing:%s Route:%s Traffic:%s Attack:%s Overlap:%s Stuck:%.1f/%.1f PWatch:%.1f/%d Starts:%d | Reform:%s(%d) | Last:%s"),
		*CombatStateName,
		MovementMode,
		ResolvedTargetRefreshInterval,
		IsValid(CurrentTarget) ? *CurrentTarget->GetName() : TEXT("None"),
		TargetHeldTime,
		*SlotName,
		CurrentSlotIndex,
		CorridorSide,
		CorridorLane,
		static_cast<unsigned long long>(QueueSequence),
		LastDistanceToSlot,
		ControlledEnemy->GetVelocity().Size2D(),
		GaitIntent,
		FacingMode,
		*RouteStageName,
		*TrafficRoleName,
		*AttackModeName,
		bOverlapRecoveryActive ? TEXT("Escape") : TEXT("None"),
		StuckElapsed,
		NoProgressElapsed,
		PursuitWatchdogNoProgressElapsed,
		PursuitWatchdogRecoveryCount,
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
