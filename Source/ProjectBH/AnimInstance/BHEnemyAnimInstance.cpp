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
