// Copyright ProjectBH. All Rights Reserved.

#include "BHEnemy.h"

#include "DetourCrowdAIController.h"

ABHEnemy::ABHEnemy()
{
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = ADetourCrowdAIController::StaticClass();
}
