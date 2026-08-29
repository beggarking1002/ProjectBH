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

	/** Stable locomotion gate. It enters and exits movement at different speeds to prevent state flicker. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData")
	bool bShouldMove = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MoveEnterSpeed = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MoveExitSpeed = 5.0f;
};
