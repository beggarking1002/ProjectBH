// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "DataAsset_EnemyConfig.generated.h"

class UAnimMontage;
class UGameplayEffect;
class UStaticMesh;

/** Coarse body-size policy consumed by navigation and engagement slots. */
UENUM(BlueprintType)
enum class EBHEnemySizeClass : uint8
{
	Normal,
	Large
};

/** Asset bindings for one attack. Numeric combat rules are read through AttackDefinition. */
USTRUCT(BlueprintType)
struct PROJECTBH_API FBHEnemyAttackConfig
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FName AttackId = TEXT("BasicAttack");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (RowType = "/Script/ProjectBH.BHAttackDefinitionRow"))
	FDataTableRowHandle AttackDefinition;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UAnimMontage> Montage;

	/** Optional sections randomly selected by the authority for this attack. Empty plays the montage from its beginning. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<FName> MontageSections;

	/** Instant effect whose damage magnitude is supplied with Data.Damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UGameplayEffect> DamageEffect;
};

/** One cosmetic weapon candidate that can be selected when an enemy enters the world. */
USTRUCT(BlueprintType)
struct PROJECTBH_API FBHEnemyWeaponOption
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UStaticMesh> WeaponMesh;

	/** Relative selection weight. Zero disables this entry without removing it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float SelectionWeight = 1.0f;

	/** Per-mesh correction applied after attaching to WeaponSocketName. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FTransform RelativeTransform = FTransform::Identity;
};

/** Fixed-distance Root Motion charge used by a Large enemy. */
USTRUCT(BlueprintType)
struct PROJECTBH_API FBHEnemyChargeConfig
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	bool bEnabled = false;

	/** Entry in Attacks whose full-body Montage contains the authored Root Motion. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FName AttackId = TEXT("ChargeAttack");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumStartDistance = 400.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumStartDistance = 850.0f;

	/** Maximum movement lead; zero aims at the current position. No mid-charge homing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "s"))
	float MaximumPredictionTime = 0.75f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumPredictionDistance = 300.0f;

	/** Lets the attack start when its authored travel ends this far short of the target center. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "cm"))
	float TargetReachTolerance = 120.0f;

	/** Rejects a Montage whose authored Root Motion drifts farther sideways than this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumRootMotionLateralDrift = 25.0f;

	/** Rejects a Montage whose authored Root Motion turns farther than this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float MaximumRootMotionYaw = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumTargetHeightDifference = 120.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "s"))
	float Cooldown = 6.0f;

	/** Extra horizontal clearance required around the Large enemy's capsule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "cm"))
	float PathClearancePadding = 15.0f;

	/** Maximum allowed difference between the authored endpoint and its NavMesh projection. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "cm"))
	float DestinationProjectionTolerance = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FVector NavProjectionExtent = FVector(150.0f, 150.0f, 250.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "cm"))
	float KnockAsideRadiusPadding = 30.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "cm/s"))
	float KnockAsideHorizontalSpeed = 900.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "cm/s"))
	float KnockAsideForwardSpeed = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "cm/s"))
	float KnockAsideVerticalSpeed = 250.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "s"))
	float KnockAsideStaggerDuration = 0.8f;

	/** Large enemies resist another Large enemy's charge by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	bool bKnockAsideLargeEnemies = false;
};

/** Per-enemy movement settings and mappings from attack IDs to animation/effect assets. */
UCLASS(BlueprintType)
class PROJECTBH_API UDataAsset_EnemyConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Gameplay size policy. This does not require a separate Enemy C++ subclass. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	EBHEnemySizeClass SizeClass = EBHEnemySizeClass::Normal;

	/** Optional runtime capsule radius. Zero preserves the value configured on the Enemy Blueprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "0.0", Units = "cm"))
	float CapsuleRadiusOverride = 0.0f;

	/** Optional runtime capsule half height. Zero preserves the value configured on the Enemy Blueprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "0.0", Units = "cm"))
	float CapsuleHalfHeightOverride = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxWalkSpeed = 300.0f;

	/** Socket on the character mesh used by every cosmetic weapon option. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FName WeaponSocketName = TEXT("WeaponSocket_R");

	/** Server selects one valid entry whenever this enemy spawns or is reactivated from its pool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment", meta = (TitleProperty = "WeaponMesh"))
	TArray<FBHEnemyWeaponOption> WeaponOptions;

	/** Capacity consumed from the currently active Attack row. Normal enemies use one; Large enemies normally use two. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement", meta = (ClampMin = "1", UIMin = "1"))
	int32 AttackSlotCost = 1;

	/** Minimum center distance from every other occupied Attack slot. Zero preserves legacy spacing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement", meta = (ClampMin = "0.0", Units = "cm"))
	float AttackSlotExclusionRadius = 0.0f;

	/** Maximum simultaneous Attack owners of this size class. Zero means unlimited. Large enemies normally use one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Engagement", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxConcurrentAttackersOfSize = 0;

	/** Extra time allowed after the expected montage duration before forcing recovery. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", Units = "s"))
	float AttackMontageFailSafeGrace = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Charge")
	FBHEnemyChargeConfig Charge;

	/** Default stagger time after a successful damaging hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reaction", meta = (ClampMin = "0.0", Units = "s"))
	float StaggerDuration = 0.6f;

	/** Optional. If unset, AnimBP can present the Staggered state directly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reaction")
	TObjectPtr<UAnimMontage> HitReactMontage;

	/** Optional. If unset, AnimBP can present the Dead state directly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reaction")
	TObjectPtr<UAnimMontage> DeathMontage;

	/** Delay before the dead enemy stops blocking navigation with its capsule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reaction", meta = (ClampMin = "0.0", Units = "s"))
	float DeathCollisionDisableDelay = 0.2f;

	/** Time before the dead actor is removed. Zero keeps it in the level. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reaction", meta = (ClampMin = "0.0", Units = "s"))
	float DeadActorLifeSpan = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	FName DefaultAttackId = TEXT("BasicAttack");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (TitleProperty = "AttackId"))
	TArray<FBHEnemyAttackConfig> Attacks;

	const FBHEnemyAttackConfig* FindAttackById(FName AttackId) const;
	const FBHEnemyAttackConfig* FindDefaultAttack() const;
};
