// Copyright ProjectBH. All Rights Reserved.

#include "BHBaseCharacter.h"

#include "AbilitySystem/BHAbilitySystemComponent.h"
#include "AbilitySystem/BHAttributeSet.h"

ABHBaseCharacter::ABHBaseCharacter()
{
	bReplicates = true;
	SetReplicateMovement(true);

	BHAbilitySystemComponent = CreateDefaultSubobject<UBHAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	BHAbilitySystemComponent->SetIsReplicated(true);
	BHAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	BHAttributeSet = CreateDefaultSubobject<UBHAttributeSet>(TEXT("AttributeSet"));
}

UAbilitySystemComponent* ABHBaseCharacter::GetAbilitySystemComponent() const
{
	return BHAbilitySystemComponent;
}

void ABHBaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitializeAbilityActorInfo();
}

void ABHBaseCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	InitializeAbilityActorInfo();
}

void ABHBaseCharacter::InitializeAbilityActorInfo()
{
	check(BHAbilitySystemComponent);
	BHAbilitySystemComponent->InitAbilityActorInfo(this, this);
}
