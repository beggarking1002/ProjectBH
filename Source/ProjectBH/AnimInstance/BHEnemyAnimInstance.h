// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BHCharacterAnimInstance.h"
#include "ProjectBH/Enemies/BHEnemy.h"
#include "BHEnemyAnimInstance.generated.h"

class ABHEnemy;

/**
 * Animation instance base for AI enemies.
 * Shared locomotion data is inherited from UBHCharacterAnimInstance.
 */
UCLASS()
class PROJECTBH_API UBHEnemyAnimInstance : public UBHCharacterAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;

protected:
	/** Typed owner reference for enemy-specific animation state added by child classes. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|References")
	TObjectPtr<ABHEnemy> OwningEnemy;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Combat")
	EBHEnemyCombatState CombatState = EBHEnemyCombatState::Chasing;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Combat")
	bool bIsStaggered = false;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Combat")
	bool bIsDead = false;

	/** True while the enemy is in its normal pursuit/formation state. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Combat")
	bool bIsChasing = true;

	/** True while locomotion must remain the base pose beneath the attack montage. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Combat")
	bool bUseMovingUpperBodyAttack = false;

	/** True after the enemy physically reached a formation slot for its current engagement. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
	bool bHasJoinedFormation = false;

	/** True while a joined enemy is running to recover a large gap to its reserved slot. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
	bool bNeedsFormationCatchUp = false;

	/** AI movement intent. Wait/Holding/Pending moves deliberately keep this false. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
	bool bWantsRunLocomotion = false;

	/** Smoothed speed measured from real world-position change, not requested crowd velocity. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
	float ObservedGroundSpeed = 0.0f;

	/** Stable locomotion gate. It enters and exits movement at different speeds to prevent state flicker. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
	bool bShouldMove = false;

	/** Drives Run during initial approach or formation catch-up while actually moving. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
	bool bShouldUseRunLocomotion = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MoveEnterSpeed = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MoveExitSpeed = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData", meta = (ClampMin = "0.0"))
	float ObservedSpeedInterpRate = 12.0f;

	/** Larger one-frame jumps are pool/teleport events and must not start locomotion. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData", meta = (ClampMin = "0.0", Units = "cm"))
	float ObservedTeleportDistance = 1000.0f;

private:
	FVector PreviousObservedLocation = FVector::ZeroVector;
	bool bHasPreviousObservedLocation = false;
};
