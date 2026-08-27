// Copyright ProjectBH. All Rights Reserved.

#include "BHCrowdEnemyAIController.h"

#include "../BHHeroCharacter.h"
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
	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);
	CurrentTarget = nullptr;

	Super::OnUnPossess();
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
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		CurrentTarget = ClosestHero;
	}

	if (!IsValid(CurrentTarget))
	{
		return;
	}

	SetFocus(CurrentTarget, EAIFocusPriority::Gameplay);

	const float AttackStartRange = ControlledEnemy->GetAttackStartRange();
	const float DistanceSquared2D = FVector::DistSquared2D(GetPawn()->GetActorLocation(), CurrentTarget->GetActorLocation());
	if (DistanceSquared2D <= FMath::Square(AttackStartRange))
	{
		StopMovement();
		ControlledEnemy->TryStartBasicAttack(CurrentTarget);
		return;
	}

	if (bTargetChanged || GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		RequestMoveToCurrentTarget(AttackStartRange);
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

void ABHCrowdEnemyAIController::RequestMoveToCurrentTarget(float AcceptanceRadius)
{
	if (!IsValid(CurrentTarget))
	{
		return;
	}

	const EPathFollowingRequestResult::Type RequestResult = MoveToActor(
		CurrentTarget,
		AcceptanceRadius,
		false,
		true,
		true,
		nullptr,
		true);

	if (RequestResult == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogProjectBH, Warning, TEXT("%s failed to find a NavMesh path to %s."), *GetName(), *CurrentTarget->GetName());
	}
}
