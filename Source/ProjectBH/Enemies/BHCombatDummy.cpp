// Copyright ProjectBH. All Rights Reserved.

#include "BHCombatDummy.h"

#include "../AbilitySystem/BHAbilitySystemComponent.h"
#include "../AbilitySystem/BHAttributeSet.h"
#include "../ProjectBH.h"
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
	GetBHAbilitySystemComponent()->GetGameplayAttributeValueChangeDelegate(UBHAttributeSet::GetHealthAttribute()).AddUObject(this, &ThisClass::HandleHealthChanged);
}

void ABHCombatDummy::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	if (!HasAuthority() || ChangeData.NewValue >= ChangeData.OldValue)
	{
		return;
	}

	UE_LOG(LogProjectBH, Display, TEXT("CombatDummy '%s' took %.1f damage. Health: %.1f -> %.1f"),
		*GetName(), ChangeData.OldValue - ChangeData.NewValue, ChangeData.OldValue, ChangeData.NewValue);
}
