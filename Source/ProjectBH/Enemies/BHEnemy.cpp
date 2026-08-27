// Copyright ProjectBH. All Rights Reserved.

#include "BHEnemy.h"

#include "../AI/BHCrowdEnemyAIController.h"
#include "GameFramework/CharacterMovementComponent.h"

ABHEnemy::ABHEnemy()
{
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = ABHCrowdEnemyAIController::StaticClass();

	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = true;
}

void ABHEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || !bWalkLeftForAnimationPreview)
	{
		return;
	}

	GetCharacterMovement()->MaxWalkSpeed = AnimationPreviewWalkSpeed;
	AddMovementInput(-GetActorRightVector(), 1.0f, true);
}
