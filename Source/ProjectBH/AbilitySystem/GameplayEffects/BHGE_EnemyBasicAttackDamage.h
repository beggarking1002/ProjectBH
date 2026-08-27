// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "BHGE_EnemyBasicAttackDamage.generated.h"

/** Instant server-authoritative damage effect for the first enemy melee attack slice. */
UCLASS()
class PROJECTBH_API UBHGE_EnemyBasicAttackDamage : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UBHGE_EnemyBasicAttackDamage();
};
