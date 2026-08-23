// Copyright ProjectBH. All Rights Reserved.

#include "BHCombatDummy.h"

#include "../AbilitySystem/BHAbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ABHCombatDummy::ABHCombatDummy()
{
	AutoPossessAI = EAutoPossessAI::Disabled;
	GetCharacterMovement()->DisableMovement();
}

void ABHCombatDummy::BeginPlay()
{
	Super::BeginPlay();
	GetBHAbilitySystemComponent()->InitAbilityActorInfo(this, this);
}
