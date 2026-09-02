// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "BHAttackDefinition.generated.h"

/** How an attack gathers hit candidates during its active timing. */
UENUM(BlueprintType)
enum class EBHAttackHitDetectionMode : uint8
{
	/** Prototype mode: validate the committed target against a range and facing cone on one notify frame. */
	CommittedTargetCone UMETA(DisplayName = "Committed Target Cone"),

	/** Planned mode: sweep weapon sockets throughout an AnimNotifyState active window. */
	WeaponSocketSweep UMETA(DisplayName = "Weapon Socket Sweep")
};

/**
 * Designer-authored combat rules for one attack.
 *
 * Asset references live in the enemy config Data Asset. This row contains only
 * reusable balance and hit-validation rules so they can be compared and tuned
 * in a Data Table.
 */
USTRUCT(BlueprintType)
struct PROJECTBH_API FBHAttackDefinitionRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Health damage passed to the Gameplay Effect as a positive design value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float BaseDamage = 10.0f;

	/** Reserved for the guard resolver. Not consumed by the current prototype hit path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float GuardDamage = 10.0f;

	/** Whether the future combat resolver may resolve this attack as Guarded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defense")
	bool bBlockable = true;

	/** Candidate-gathering implementation used by this attack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit")
	EBHAttackHitDetectionMode HitDetectionMode = EBHAttackHitDetectionMode::CommittedTargetCone;

	/** Distance at which a settled enemy commits to a stationary full-body attack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit", meta = (ClampMin = "0.0", Units = "cm"))
	float AttackStartRange = 150.0f;

	/** Allows the Attack-slot owner to attack while its locomotion path is still moving. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moving Attack")
	bool bAllowMovingAttack = false;

	/** Earlier commitment range used only by a moving upper-body attack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moving Attack", meta = (ClampMin = "0.0", Units = "cm"))
	float MovingAttackStartRange = 300.0f;

	/** Maximum horizontal distance accepted by the prototype target-cone hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit|Target Cone", meta = (ClampMin = "0.0", Units = "cm"))
	float TargetConeRange = 225.0f;

	/** Half-angle of the prototype target-cone hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit|Target Cone", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float TargetConeHalfAngle = 60.0f;

	/** Maximum vertical separation accepted by the prototype target-cone hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit|Target Cone", meta = (ClampMin = "0.0", Units = "cm"))
	float TargetConeHeightTolerance = 120.0f;

	/** Server lockout after the attack montage has fully ended. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", Units = "s"))
	float RecoveryDuration = 0.75f;
};
