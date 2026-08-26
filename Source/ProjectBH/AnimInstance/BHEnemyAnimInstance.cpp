// Copyright ProjectBH. All Rights Reserved.

#include "BHEnemyAnimInstance.h"

#include "ProjectBH/Enemies/BHEnemy.h"

void UBHEnemyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	OwningEnemy = Cast<ABHEnemy>(OwningCharacter);
}
