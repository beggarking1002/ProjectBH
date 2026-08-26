// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BHCharacterAnimInstance.h"
#include "BHHeroAnimInstance.generated.h"

class ABHHeroCharacter;
/**
 * 
 */
UCLASS()
class PROJECTBH_API UBHHeroAnimInstance : public UBHCharacterAnimInstance
{
	GENERATED_BODY()
	
public:
	virtual void NativeInitializeAnimation() override;
	
protected:
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|References")
	ABHHeroCharacter* OwningHeroCharacter;
};
