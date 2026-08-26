// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../BHBaseCharacter.h"
#include "BHEnemy.generated.h"

/**
 * Common network-ready base for combat enemies.
 *
 * Uses UE's Detour Crowd controller for path-aware local avoidance. Combat
 * engagement slots and concrete monster behavior are added by child classes.
 */
UCLASS(Abstract, Blueprintable)
class PROJECTBH_API ABHEnemy : public ABHBaseCharacter
{
	GENERATED_BODY()

public:
	ABHEnemy();
};
