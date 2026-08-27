// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DetourCrowdAIController.h"
#include "BHCrowdEnemyAIController.generated.h"

class ABHHeroCharacter;

/**
 * Minimal server-authoritative controller for NavMesh enemy pursuit.
 *
 * Detour Crowd handles path-aware local avoidance. This controller owns only
 * target selection and the high-level MoveTo request; combat slots are added
 * in a later vertical-slice step.
 */
UCLASS(Blueprintable)
class PROJECTBH_API ABHCrowdEnemyAIController : public ADetourCrowdAIController
{
	GENERATED_BODY()

public:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	UFUNCTION(BlueprintPure, Category = "AI|Target")
	ABHHeroCharacter* GetCurrentTarget() const { return CurrentTarget; }

protected:
	/** Target validation and stalled-move retry interval. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "0.1", Units = "s"))
	float TargetRefreshInterval = 0.5f;

private:
	void RefreshTargetAndMove();
	ABHHeroCharacter* FindClosestPlayerHero() const;
	void RequestMoveToCurrentTarget(float AcceptanceRadius);

	UPROPERTY(Transient)
	TObjectPtr<ABHHeroCharacter> CurrentTarget;

	FTimerHandle TargetRefreshTimerHandle;
};
