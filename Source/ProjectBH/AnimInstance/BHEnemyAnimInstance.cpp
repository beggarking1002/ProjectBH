// Copyright ProjectBH. All Rights Reserved.

#include "BHEnemyAnimInstance.h"

#include "ProjectBH/Enemies/BHEnemy.h"

void UBHEnemyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	OwningEnemy = Cast<ABHEnemy>(OwningCharacter);
}

void UBHEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (!OwningEnemy)
	{
		return;
	}

	CombatState = OwningEnemy->GetCombatState();
	bIsStaggered = CombatState == EBHEnemyCombatState::Staggered;
	bIsDead = CombatState == EBHEnemyCombatState::Dead;
}

void UBHEnemyAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	if (!OwningCharacter || !OwningMovementComponent)
	{
		return;
	}

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

	// Enemy ABPs currently use bHasAcceleration for Idle/Jog transitions.
	// Preserve that wiring while giving it the stable speed-based meaning above.
	bHasAcceleration = bShouldMove;
}
