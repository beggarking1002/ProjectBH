// Fill out your copyright notice in the Description page of Project Settings.


#include "BHCharacterAnimInstance.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Math/RotationMatrix.h"
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

	const FVector NormalizedVelocity = OwningCharacter->GetVelocity().GetSafeNormal2D();
	if (NormalizedVelocity.IsNearlyZero())
	{
		Direction = 0.0f;
	}
	else
	{
		const FRotationMatrix CharacterRotationMatrix(OwningCharacter->GetActorRotation());
		const float ForwardDot = FVector::DotProduct(CharacterRotationMatrix.GetUnitAxis(EAxis::X), NormalizedVelocity);
		const float RightDot = FVector::DotProduct(CharacterRotationMatrix.GetUnitAxis(EAxis::Y), NormalizedVelocity);
		Direction = FMath::RadiansToDegrees(FMath::Atan2(RightDot, ForwardDot));
	}

	bHasAcceleration = OwningMovementComponent->GetCurrentAcceleration().SizeSquared2D()>0.f;
}
