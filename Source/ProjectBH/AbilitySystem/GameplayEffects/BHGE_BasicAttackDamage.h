// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "BHGE_BasicAttackDamage.generated.h"

/** Instant server-authoritative damage effect used by the first Greystone sword-combo slice. */
UCLASS()
class PROJECTBH_API UBHGE_BasicAttackDamage : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UBHGE_BasicAttackDamage();
};
