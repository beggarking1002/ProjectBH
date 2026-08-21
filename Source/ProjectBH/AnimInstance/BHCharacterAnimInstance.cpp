// Fill out your copyright notice in the Description page of Project Settings.


#include "BHCharacterAnimInstance.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "ProjectBH/BHBaseCharacter.h"

void UBHCharacterAnimInstance::NativeInitializeAnimation()
{
	OwningCharacter = Cast<ABHBaseCharacter>(TryGetPawnOwner());

	if (OwningCharacter)
	{
		OwningMovementComponent = OwningCharacter->GetCharacterMovement();
	}
}

void UBHCharacterAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	if (!OwningCharacter || !OwningMovementComponent)
	{
		return;
	}

	GroundSpeed = OwningCharacter->GetVelocity().Size2D();

	bHasAcceleration = OwningMovementComponent->GetCurrentAcceleration().SizeSquared2D()>0.f;
}
