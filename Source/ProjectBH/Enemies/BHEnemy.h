// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../BHBaseCharacter.h"
#include "BHEnemy.generated.h"

class UAnimMontage;
class UDataAsset_EnemyConfig;
struct FBHAttackDefinitionRow;
struct FBHEnemyAttackConfig;

/** Replicated high-level state used by enemy animation and combat presentation. */
UENUM(BlueprintType)
enum class EBHEnemyCombatState : uint8
{
	Chasing,
	Attacking,
	Recovering
};

/**
 * Common network-ready base for combat enemies.
 *
 * Uses UE's Detour Crowd controller for path-aware local avoidance. Combat
 * engagement slots and concrete monster behavior are added by child classes.
 */
UCLASS(Abstract, Blueprintable)
class PROJECTBH_API ABHEnemy : public ABHBaseCharacter
{
	GENERATED_BODY()

public:
	ABHEnemy();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Starts one server-authoritative melee attack against a fixed target. */
	bool TryStartBasicAttack(AActor* TargetActor);

	/** Called by BH Enemy Attack Hit notify. Damage validation and application run on the server only. */
	void PerformBasicAttackHit();

	UFUNCTION(BlueprintPure, Category = "Combat|Enemy")
	EBHEnemyCombatState GetCombatState() const { return CombatState; }

	UFUNCTION(BlueprintPure, Category = "Combat|Enemy")
	bool IsAttackLocked() const { return CombatState != EBHEnemyCombatState::Chasing; }

	UFUNCTION(BlueprintPure, Category = "Combat|Enemy")
	float GetAttackStartRange() const;

protected:
	/** Movement and attack asset bindings. Numeric attack rules come from its DataTable row handles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDataAsset_EnemyConfig> EnemyConfigDataAsset;

	/** Temporary animation-integration test: move in local left direction on the authority. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Animation", meta = (AllowPrivateAccess = "true"))
	bool bWalkLeftForAnimationPreview = false;

	/** Speed used only while bWalkLeftForAnimationPreview is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Animation", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float AnimationPreviewWalkSpeed = 150.0f;

private:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayBasicAttack(UAnimMontage* AttackMontage, FName MontageSection);

	void HandleAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void BeginAttackRecovery();
	void FinishAttackRecovery();
	bool IsAttackTargetInHitArea() const;
	const FBHEnemyAttackConfig* GetAttackConfig(FName AttackId) const;
	const FBHEnemyAttackConfig* GetDefaultAttackConfig() const;
	const FBHAttackDefinitionRow* GetAttackDefinition(const FBHEnemyAttackConfig& AttackConfig) const;
	const FBHAttackDefinitionRow* GetActiveAttackDefinition() const;
	FName SelectRandomMontageSection(const FBHEnemyAttackConfig& AttackConfig) const;
	void SetCombatState(EBHEnemyCombatState NewState);

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Combat|Enemy", meta = (AllowPrivateAccess = "true"))
	EBHEnemyCombatState CombatState = EBHEnemyCombatState::Chasing;

	/** Frozen for one attack so a target switch cannot redirect a hit already in progress. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> AttackTarget;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveAttackMontage;

	FName ActiveAttackId = NAME_None;
	FName ActiveAttackMontageSection = NAME_None;

	FTimerHandle AttackRecoveryTimerHandle;
	bool bLoggedInvalidAttackConfig = false;
	bool bHasAppliedDamageThisAttack = false;
};
