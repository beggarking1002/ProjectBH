// Copyright ProjectBH. All Rights Reserved.

#include "BHEnemyAnimInstance.h"

#include "ProjectBH/Enemies/BHEnemy.h"

void UBHEnemyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	OwningEnemy = Cast<ABHEnemy>(OwningCharacter);
	if (OwningEnemy)
	{
		PreviousObservedLocation = OwningEnemy->GetActorLocation();
		bHasPreviousObservedLocation = true;
	}
}

void UBHEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (!OwningEnemy)
	{
		return;
	}

	CombatState = OwningEnemy->GetCombatState();
	bIsChasing = CombatState == EBHEnemyCombatState::Chasing;
	bUseMovingUpperBodyAttack = OwningEnemy->UsesMovingUpperBodyAttack()
		&& (CombatState == EBHEnemyCombatState::Attacking
			|| CombatState == EBHEnemyCombatState::Recovering);
	bIsHighGroundDropping = OwningEnemy->IsHighGroundDropActive();
	bHasJoinedFormation = OwningEnemy->HasJoinedFormation();
	bNeedsFormationCatchUp = OwningEnemy->NeedsFormationCatchUp();
	bWantsRunLocomotion = OwningEnemy->WantsRunLocomotion();
	bIsStaggered = CombatState == EBHEnemyCombatState::Staggered;
	bIsDead = CombatState == EBHEnemyCombatState::Dead;

	const FVector CurrentLocation = OwningEnemy->GetActorLocation();
	if (!bHasPreviousObservedLocation
		|| DeltaSeconds <= UE_SMALL_NUMBER
		|| DeltaSeconds > 0.25f
		|| FVector::DistSquared2D(CurrentLocation, PreviousObservedLocation)
			> FMath::Square(FMath::Max(0.0f, ObservedTeleportDistance)))
	{
		PreviousObservedLocation = CurrentLocation;
		bHasPreviousObservedLocation = true;
		ObservedGroundSpeed = 0.0f;
		return;
	}

	const float RawObservedSpeed = FVector::Dist2D(CurrentLocation, PreviousObservedLocation)
		/ DeltaSeconds;
	ObservedGroundSpeed = FMath::FInterpTo(
		ObservedGroundSpeed,
		RawObservedSpeed,
		DeltaSeconds,
		FMath::Max(0.0f, ObservedSpeedInterpRate));
	PreviousObservedLocation = CurrentLocation;
}

void UBHEnemyAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	if (!OwningCharacter || !OwningMovementComponent)
	{
		return;
	}
	// Detour can report non-zero CharacterMovement velocity while avoidance keeps
	// the actor in place. Enemy animation uses real translation so blocked agents
	// settle to Idle instead of running on the spot.
	GroundSpeed = ObservedGroundSpeed;

	const float ExitSpeed = FMath::Max(0.0f, MoveExitSpeed);
	const float EnterSpeed = FMath::Max(ExitSpeed, MoveEnterSpeed);
	if (bShouldMove)
	{
		bShouldMove = GroundSpeed > ExitSpeed;
	}
	else
	{
		bShouldMove = GroundSpeed >= EnterSpeed;
	}
	bShouldUseRunLocomotion = (bIsChasing || bUseMovingUpperBodyAttack)
		&& bShouldMove
		&& bWantsRunLocomotion;

	// Enemy ABPs currently use bHasAcceleration for Idle/Jog transitions.
	// Preserve that wiring while giving it the stable speed-based meaning above.
	bHasAcceleration = bShouldMove;
}
