// Copyright ProjectBH. All Rights Reserved.

#include "BHGameMode.h"

#include "BHHeroCharacter.h"
#include "BHHeroController.h"

ABHGameMode::ABHGameMode()
{
	DefaultPawnClass = ABHHeroCharacter::StaticClass();
	PlayerControllerClass = ABHHeroController::StaticClass();
}
