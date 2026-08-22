// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "BHBaseCharacter.generated.h"

class UBHAttributeSet;
class UBHAbilitySystemComponent;
/** Common network-ready character base for heroes and future AI characters. */
UCLASS(Abstract)
class PROJECTBH_API ABHBaseCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ABHBaseCharacter();
	
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	
protected:
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_Controller() override;

	void InitializeAbilityActorInfo();
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<UBHAbilitySystemComponent> BHAbilitySystemComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<UBHAttributeSet> BHAttributeSet;
	
public:
	FORCEINLINE UBHAbilitySystemComponent* GetBHAbilitySystemComponent() const {return BHAbilitySystemComponent;}
	FORCEINLINE UBHAttributeSet* GetBHAttributeSet() const {return BHAttributeSet;}
};
