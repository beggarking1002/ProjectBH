// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../Components/Combat/CombatEngagementSlotComponent.h"
#include "DetourCrowdAIController.h"
#include "BHCrowdEnemyAIController.generated.h"

class ABHHeroCharacter;
class ABHEnemy;
class UCombatEngagementSlotComponent;

/**
 * Server-authoritative controller for NavMesh enemy engagement.
 *
 * Detour Crowd handles path-aware local avoidance. This controller selects a
 * player target, reserves one of that player's combat slots, and moves to the
 * reserved NavMesh position instead of moving directly to the player actor.
 */
UCLASS(Blueprintable)
class PROJECTBH_API ABHCrowdEnemyAIController : public ADetourCrowdAIController
{
	GENERATED_BODY()

public:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

	UFUNCTION(BlueprintPure, Category = "AI|Target")
	ABHHeroCharacter* GetCurrentTarget() const { return CurrentTarget; }

protected:
	/** Target validation and stalled-move retry interval. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "0.1", Units = "s"))
	float TargetRefreshInterval = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float SlotAcceptanceRadius = 15.0f;

	/** Reissue MoveTo after the moving player's reserved slot has shifted this far. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Engagement Slots", meta = (ClampMin = "1.0", Units = "cm"))
	float SlotRepathDistance = 50.0f;

private:
	void RefreshTargetAndMove();
	ABHHeroCharacter* FindClosestPlayerHero() const;
	bool AcquireCombatSlot(ABHEnemy* ControlledEnemy);
	void ReleaseCurrentCombatSlot();
	void RequestMoveToReservedSlot(const FVector& SlotLocation);

	UPROPERTY(Transient)
	TObjectPtr<ABHHeroCharacter> CurrentTarget;

	UPROPERTY(Transient)
	TObjectPtr<UCombatEngagementSlotComponent> CurrentSlotComponent;

	EBHCombatSlotType CurrentSlotType = EBHCombatSlotType::None;
	int32 CurrentSlotIndex = INDEX_NONE;
	TWeakObjectPtr<AActor> CurrentSlotRequester;
	FVector LastRequestedSlotLocation = FVector::ZeroVector;
	bool bHasRequestedSlotMove = false;

	FTimerHandle TargetRefreshTimerHandle;
};
