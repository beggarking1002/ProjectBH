// Fill out your copyright notice in the Description page of Project Settings.


#include "BHHeroAnimInstance.h"

#include "ProjectBH/BHHeroCharacter.h"

void UBHHeroAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	if (OwningCharacter)
	{
		OwningHeroCharacter = Cast<ABHHeroCharacter>(OwningCharacter);
	}
}
