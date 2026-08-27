// Copyright ProjectBH. All Rights Reserved.

#include "BHCrowdEnemyAIController.h"

#include "../BHHeroCharacter.h"
#include "../Components/Combat/CombatEngagementSlotComponent.h"
#include "../Enemies/BHEnemy.h"
#include "../ProjectBH.h"
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
	ReleaseCurrentCombatSlot();
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
		ReleaseCurrentCombatSlot();
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
		return;
	}

	ABHHeroCharacter* ClosestHero = FindClosestPlayerHero();
	const bool bTargetChanged = ClosestHero != CurrentTarget;
	if (bTargetChanged)
	{
		ReleaseCurrentCombatSlot();
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		CurrentTarget = ClosestHero;
	}

	if (!IsValid(CurrentTarget))
	{
		ReleaseCurrentCombatSlot();
		return;
	}

	SetFocus(CurrentTarget, EAIFocusPriority::Gameplay);

	if (!AcquireCombatSlot(ControlledEnemy))
	{
		StopMovement();
		return;
	}

	FVector SlotLocation;
	if (!CurrentSlotComponent->GetReservedSlot(GetPawn(), CurrentSlotType, CurrentSlotIndex, SlotLocation))
	{
		ReleaseCurrentCombatSlot();
		StopMovement();
		return;
	}

	const float DistanceToSlotSquared = FVector::DistSquared2D(GetPawn()->GetActorLocation(), SlotLocation);
	const bool bAtReservedSlot = DistanceToSlotSquared <= FMath::Square(SlotAcceptanceRadius);
	if (CurrentSlotType == EBHCombatSlotType::Attack && bAtReservedSlot)
	{
		StopMovement();
		bHasRequestedSlotMove = false;

		const float AttackStartRange = ControlledEnemy->GetAttackStartRange();
		const float DistanceToTargetSquared = FVector::DistSquared2D(GetPawn()->GetActorLocation(), CurrentTarget->GetActorLocation());
		if (DistanceToTargetSquared <= FMath::Square(AttackStartRange))
		{
			ControlledEnemy->TryStartBasicAttack(CurrentTarget);
		}
		else
		{
			ReleaseCurrentCombatSlot();
		}
		return;
	}

	if (bAtReservedSlot)
	{
		StopMovement();
		bHasRequestedSlotMove = false;
		return;
	}

	const bool bSlotMoved = !bHasRequestedSlotMove
		|| FVector::DistSquared2D(LastRequestedSlotLocation, SlotLocation) >= FMath::Square(SlotRepathDistance);
	if (bTargetChanged || bSlotMoved || GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		RequestMoveToReservedSlot(SlotLocation);
	}
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
		ReleaseCurrentCombatSlot();
		return false;
	}

	if (CurrentSlotComponent != TargetSlotComponent)
	{
		ReleaseCurrentCombatSlot();
		CurrentSlotComponent = TargetSlotComponent;
	}

	const float MaximumAttackSlotDistance = FMath::Max(
		0.0f,
		ControlledEnemy->GetAttackStartRange() - SlotAcceptanceRadius);
	if (!CurrentSlotComponent->TryReserveAttackSlot(ControlledEnemy, MaximumAttackSlotDistance))
	{
		CurrentSlotComponent->TryReserveWaitSlot(ControlledEnemy);
	}

	FVector IgnoredSlotLocation;
	const bool bHasReservation = CurrentSlotComponent->GetReservedSlot(
		ControlledEnemy,
		CurrentSlotType,
		CurrentSlotIndex,
		IgnoredSlotLocation);
	CurrentSlotRequester = bHasReservation ? ControlledEnemy : nullptr;
	return bHasReservation;
}

void ABHCrowdEnemyAIController::ReleaseCurrentCombatSlot()
{
	if (CurrentSlotComponent && CurrentSlotRequester.IsValid())
	{
		CurrentSlotComponent->ReleaseSlot(CurrentSlotRequester.Get());
	}

	CurrentSlotComponent = nullptr;
	CurrentSlotType = EBHCombatSlotType::None;
	CurrentSlotIndex = INDEX_NONE;
	CurrentSlotRequester.Reset();
	LastRequestedSlotLocation = FVector::ZeroVector;
	bHasRequestedSlotMove = false;
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
		ReleaseCurrentCombatSlot();
		return;
	}

	LastRequestedSlotLocation = SlotLocation;
	bHasRequestedSlotMove = true;
}
