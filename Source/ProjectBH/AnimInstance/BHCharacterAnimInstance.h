// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BHBaseAnimInstance.h"
#include "BHCharacterAnimInstance.generated.h"

class ABHBaseCharacter;
class UCharacterMovementComponent;
/**
 * 
 */
UCLASS()
class PROJECTBH_API UBHCharacterAnimInstance : public UBHBaseAnimInstance
{
	GENERATED_BODY()
	
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;
	
protected:
	UPROPERTY()
	ABHBaseCharacter* OwningCharacter;
	
	UPROPERTY()
	UCharacterMovementComponent* OwningMovementComponent;
	
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
	float GroundSpeed;

	/** Movement direction relative to the character facing: forward 0, strafe approximately +/-90, backward +/-180. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
	float Direction;
	
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
	bool bHasAcceleration;
	
};
