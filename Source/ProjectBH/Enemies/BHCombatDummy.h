// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../BHBaseCharacter.h"
#include "BHCombatDummy.generated.h"

struct FOnAttributeChangeData;

/** Stationary networked target used to verify damage and death gameplay. */
UCLASS()
class PROJECTBH_API ABHCombatDummy : public ABHBaseCharacter
{
	GENERATED_BODY()

public:
	ABHCombatDummy();

protected:
	virtual void BeginPlay() override;

	/** Server-only diagnostic for the first combat slice. Health is still replicated separately by GAS. */
	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);
};
