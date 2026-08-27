// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "DataAsset_EnemyConfig.generated.h"

class UAnimMontage;
class UGameplayEffect;

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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxWalkSpeed = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	FName DefaultAttackId = TEXT("BasicAttack");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (TitleProperty = "AttackId"))
	TArray<FBHEnemyAttackConfig> Attacks;

	const FBHEnemyAttackConfig* FindAttackById(FName AttackId) const;
	const FBHEnemyAttackConfig* FindDefaultAttack() const;
};
