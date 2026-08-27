// Copyright ProjectBH. All Rights Reserved.

#include "BHCrowdEnemyAIController.h"

#include "../BHHeroCharacter.h"
#include "../Components/Combat/CombatEngagementSlotComponent.h"
#include "../Enemies/BHEnemy.h"
#include "../ProjectBH.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"

void ABHCrowdEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!HasAuthority())
	{
		return;
	}

	RefreshTargetAndMove();
	GetWorldTimerManager().SetTimer(
		TargetRefreshTimerHandle,
		this,
		&ThisClass::RefreshTargetAndMove,
		TargetRefreshInterval,
		true);
}

void ABHCrowdEnemyAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(TargetRefreshTimerHandle);
	ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::UnPossessed);
	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);
	CurrentTarget = nullptr;

	Super::OnUnPossess();
}

void ABHCrowdEnemyAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

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
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	ABHHeroCharacter* ClosestHero = FindClosestPlayerHero();
	const bool bTargetChanged = ClosestHero != CurrentTarget;
	if (bTargetChanged)
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::TargetChanged);
		SlotRequestBlockedUntil = 0.0f;
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		CurrentTarget = ClosestHero;
	}

	if (!IsValid(CurrentTarget))
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::TargetLost);
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	SetFocus(CurrentTarget, EAIFocusPriority::Gameplay);
	if (GetWorld()->GetTimeSeconds() < SlotRequestBlockedUntil)
	{
		StopMovement();
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	if (!AcquireCombatSlot(ControlledEnemy))
	{
		StopMovement();
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	FVector SlotLocation;
	if (!CurrentSlotComponent->GetReservedSlot(GetPawn(), CurrentSlotType, CurrentSlotIndex, SlotLocation))
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::ReservationInvalid);
		StopMovement();
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	const float DistanceToSlotSquared = FVector::DistSquared2D(GetPawn()->GetActorLocation(), SlotLocation);
	const float DistanceToSlot = FMath::Sqrt(DistanceToSlotSquared);
	LastDistanceToSlot = DistanceToSlot;
	const bool bAtReservedSlot = DistanceToSlotSquared <= FMath::Square(SlotAcceptanceRadius);
	if (CurrentSlotType == EBHCombatSlotType::Attack && bAtReservedSlot)
	{
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
		StopMovement();
		bHasRequestedSlotMove = false;
		bIsReforming = false;
		ResetStuckTracking();
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	if (bHasRequestedSlotMove
		&& UpdateStuckTracking(DistanceToSlot, ControlledEnemy->GetVelocity().Size2D()))
	{
		StopMovement();
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::Stalled, true);
		DrawDebugStatus(ControlledEnemy);
		return;
	}

	const bool bSlotMoved = !bHasRequestedSlotMove
		|| FVector::DistSquared2D(LastRequestedSlotLocation, SlotLocation) >= FMath::Square(SlotRepathDistance);
	if (bTargetChanged || bSlotMoved || GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		RequestMoveToReservedSlot(SlotLocation);
	}

	DrawDebugStatus(ControlledEnemy);
}

ABHHeroCharacter* ABHCrowdEnemyAIController::FindClosestPlayerHero() const
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !GetWorld())
	{
		return nullptr;
	}

	ABHHeroCharacter* ClosestHero = nullptr;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();

	for (TActorIterator<ABHHeroCharacter> It(GetWorld()); It; ++It)
	{
		ABHHeroCharacter* Hero = *It;
		if (!IsValid(Hero) || !Hero->IsPlayerControlled())
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared2D(ControlledPawn->GetActorLocation(), Hero->GetActorLocation());
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestHero = Hero;
		}
	}

	return ClosestHero;
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

	if (CurrentSlotComponent != TargetSlotComponent)
	{
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::TargetChanged);
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
		ResetStuckTracking();
	}

	const float MaximumAttackSlotDistance = FMath::Max(
		0.0f,
		ControlledEnemy->GetAttackStartRange() - SlotAcceptanceRadius);
	if (!CurrentSlotComponent->TryReserveAttackSlot(
		ControlledEnemy,
		MaximumAttackSlotDistance,
		GetExcludedSlotIndex(EBHCombatSlotType::Attack)))
	{
		CurrentSlotComponent->TryReserveWaitSlot(
			ControlledEnemy,
			GetExcludedSlotIndex(EBHCombatSlotType::Wait));
	}

	FVector IgnoredSlotLocation;
	const bool bHasReservation = CurrentSlotComponent->GetReservedSlot(
		ControlledEnemy,
		CurrentSlotType,
		CurrentSlotIndex,
		IgnoredSlotLocation);
	if (!bHasReservation && CurrentSlotType != EBHCombatSlotType::None)
	{
		CurrentSlotRequester = ControlledEnemy;
		ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason::ReservationInvalid, true);
		return false;
	}

	CurrentSlotRequester = bHasReservation ? ControlledEnemy : nullptr;
	if (bHasReservation
		&& (TrackedSlotType != CurrentSlotType || TrackedSlotIndex != CurrentSlotIndex))
	{
		ResetStuckTracking();
		TrackedSlotType = CurrentSlotType;
		TrackedSlotIndex = CurrentSlotIndex;
	}
	return bHasReservation;
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
		CurrentSlotComponent->ReleaseSlot(CurrentSlotRequester.Get());
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
	LastRequestedSlotLocation = FVector::ZeroVector;
	bHasRequestedSlotMove = false;
	TrackedSlotType = EBHCombatSlotType::None;
	TrackedSlotIndex = INDEX_NONE;
	ResetStuckTracking();
}

void ABHCrowdEnemyAIController::RequestMoveToReservedSlot(const FVector& SlotLocation)
{
	const EPathFollowingRequestResult::Type RequestResult = MoveToLocation(
		SlotLocation,
		SlotAcceptanceRadius,
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
		StuckElapsed += TargetRefreshInterval;
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
	const FString DebugText = FString::Printf(
		TEXT("%s | %s[%d] Dist:%.0f Stuck:%.1f Starts:%d | Reform:%s(%d) | Last:%s"),
		*CombatStateName,
		*SlotName,
		CurrentSlotIndex,
		LastDistanceToSlot,
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
		CurrentSlotType == EBHCombatSlotType::Attack ? FColor::Red : FColor::Yellow,
		TargetRefreshInterval + 0.1f,
		false,
		0.85f);
}

int32 ABHCrowdEnemyAIController::GetExcludedSlotIndex(EBHCombatSlotType SlotType) const
{
	if (!GetWorld()
		|| SlotType != ExcludedSlotType
		|| GetWorld()->GetTimeSeconds() >= ExcludedSlotUntil)
	{
		return INDEX_NONE;
	}

	return ExcludedSlotIndex;
}
