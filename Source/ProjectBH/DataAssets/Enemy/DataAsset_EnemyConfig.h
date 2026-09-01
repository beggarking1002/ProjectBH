// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "DataAsset_EnemyConfig.generated.h"

class UAnimMontage;
class UGameplayEffect;

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
