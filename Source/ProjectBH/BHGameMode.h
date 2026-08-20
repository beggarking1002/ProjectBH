// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BHGameMode.generated.h"

/** Defines the default player pawn and controller for ProjectBH game maps. */
UCLASS()
class PROJECTBH_API ABHGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABHGameMode();
};
