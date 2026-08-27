// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../Components/Combat/CombatEngagementSlotComponent.h"
#include "DetourCrowdAIController.h"
#include "BHCrowdEnemyAIController.generated.h"

class ABHHeroCharacter;
class ABHEnemy;
class UCombatEngagementSlotComponent;

UENUM(BlueprintType)
enum class EBHCombatSlotReleaseReason : uint8
{
	None,
	TargetChanged,
	TargetLost,
	UnPossessed,
	ReservationInvalid,
	AttackRangeMismatch,
	AttackStartFailed,
	MoveRequestFailed,
	PathFollowingFailed,
	Stalled,
	AttackRecoveryComplete,
	Staggered,
	Died
};

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

	/** Releases the current reservation immediately and optionally delays the next request. */
	void ReleaseCombatSlot(EBHCombatSlotReleaseReason Reason, float ReacquireDelay = 0.0f);

protected:
	/** Target validation and stalled-move retry interval. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Pursuit", meta = (ClampMin = "0.1", Units = "s"))
	float TargetRefreshInterval = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Engagement Slots", meta = (ClampMin = "0.0", Units = "cm"))
	float SlotAcceptanceRadius = 15.0f;

	/** Reissue MoveTo after the moving player's reserved slot has shifted this far. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Engagement Slots", meta = (ClampMin = "1.0", Units = "cm"))
	float SlotRepathDistance = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery", meta = (ClampMin = "0.1", Units = "s"))
	float StuckTimeout = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery", meta = (ClampMin = "0.0", Units = "cm"))
	float StuckProgressDistance = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery", meta = (ClampMin = "0.0", Units = "cm/s"))
	float StuckSpeedThreshold = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Recovery", meta = (ClampMin = "0.0", Units = "s"))
	float FailedSlotCooldown = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Debug|Crowd")
	bool bDrawCrowdDebug = true;

private:
	void RefreshTargetAndMove();
	ABHHeroCharacter* FindClosestPlayerHero() const;
	bool AcquireCombatSlot(ABHEnemy* ControlledEnemy);
	void ReleaseCurrentCombatSlot(EBHCombatSlotReleaseReason Reason, bool bTemporarilyExcludeReleasedSlot = false);
	void RequestMoveToReservedSlot(const FVector& SlotLocation);
	bool UpdateStuckTracking(float DistanceToSlot, float Speed);
	void ResetStuckTracking();
	void DrawDebugStatus(const ABHEnemy* ControlledEnemy) const;
	int32 GetExcludedSlotIndex(EBHCombatSlotType SlotType) const;

	UPROPERTY(Transient)
	TObjectPtr<ABHHeroCharacter> CurrentTarget;

	UPROPERTY(Transient)
	TObjectPtr<UCombatEngagementSlotComponent> CurrentSlotComponent;

	EBHCombatSlotType CurrentSlotType = EBHCombatSlotType::None;
	int32 CurrentSlotIndex = INDEX_NONE;
	TWeakObjectPtr<AActor> CurrentSlotRequester;
	FVector LastRequestedSlotLocation = FVector::ZeroVector;
	bool bHasRequestedSlotMove = false;
	bool bIsUsingStagedRoute = false;

	EBHCombatSlotType TrackedSlotType = EBHCombatSlotType::None;
	int32 TrackedSlotIndex = INDEX_NONE;
	float ProgressReferenceDistance = 0.0f;
	float StuckElapsed = 0.0f;
	float LastDistanceToSlot = 0.0f;
	bool bHasProgressSample = false;

	EBHCombatSlotType ExcludedSlotType = EBHCombatSlotType::None;
	int32 ExcludedSlotIndex = INDEX_NONE;
	float ExcludedSlotUntil = 0.0f;
	float SlotRequestBlockedUntil = 0.0f;
	EBHCombatSlotReleaseReason LastReleaseReason = EBHCombatSlotReleaseReason::None;
	int32 LastObservedFormationRevision = INDEX_NONE;
	int32 ReformCount = 0;
	bool bIsReforming = false;

	FTimerHandle TargetRefreshTimerHandle;
};
