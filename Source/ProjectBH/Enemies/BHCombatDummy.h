// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../BHBaseCharacter.h"
#include "BHCombatDummy.generated.h"

/** Stationary networked target used to verify damage and death gameplay. */
UCLASS()
class PROJECTBH_API ABHCombatDummy : public ABHBaseCharacter
{
	GENERATED_BODY()

public:
	ABHCombatDummy();

protected:
	virtual void BeginPlay() override;
};
