// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../BHBaseCharacter.h"
#include "../DataAssets/Enemy/DataAsset_EnemyConfig.h"
#include "BHEnemy.generated.h"

class UAnimMontage;
class ABHEnemyPoolManager;
struct FOnAttributeChangeData;
struct FBHAttackDefinitionRow;

/** Replicated high-level state used by enemy animation and combat presentation. */
UENUM(BlueprintType)
enum class EBHEnemyCombatState : uint8
{
	Chasing,
	Attacking,
	Recovering,
	Staggered,
	Dead
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
	bool IsDead() const { return CombatState == EBHEnemyCombatState::Dead; }

	/** True after this enemy has physically reached any reserved formation slot for its current target. */
	UFUNCTION(BlueprintPure, Category = "AI|Formation")
	bool HasJoinedFormation() const { return bHasJoinedFormation; }

	/** Latches formation arrival for animation and presentation. Authority only. */
	void MarkFormationJoined();

	/** Starts a fresh approach run for a new/lost engagement. Authority only. */
	void ResetFormationJoinState();

	/** True while a joined enemy is far enough from its current slot to run and catch up. */
	UFUNCTION(BlueprintPure, Category = "AI|Formation")
	bool NeedsFormationCatchUp() const { return bNeedsFormationCatchUp; }

	/** Updates the formation catch-up gait intent. Authority only. */
	void SetFormationCatchUpRequired(bool bRequired);

	/** Authoritative gait request produced by the AI controller. */
	UFUNCTION(BlueprintPure, Category = "AI|Formation")
	bool WantsRunLocomotion() const { return bWantsRunLocomotion; }

	/** Sets whether the current AI move should use Run presentation. Authority only. */
	void SetWantsRunLocomotion(bool bWantsRun);

	UFUNCTION(BlueprintPure, Category = "Combat|Enemy")
	float GetAttackStartRange() const;

	UFUNCTION(BlueprintPure, Category = "Combat|Enemy|Size")
	EBHEnemySizeClass GetEnemySizeClass() const;

	/** Capacity consumed from the target's active Attack row. */
	UFUNCTION(BlueprintPure, Category = "Combat|Enemy|Size")
	int32 GetAttackSlotCost() const;

	/** Required center clearance from other occupied Attack slots. */
	UFUNCTION(BlueprintPure, Category = "Combat|Enemy|Size")
	float GetAttackSlotExclusionRadius() const;

	/** Zero means the size class has no separate simultaneous-Attack cap. */
	UFUNCTION(BlueprintPure, Category = "Combat|Enemy|Size")
	int32 GetMaxConcurrentAttackersOfSize() const;

	UFUNCTION(BlueprintPure, Category = "Debug|Enemy")
	int32 GetSuccessfulAttackStartCount() const { return SuccessfulAttackStartCount; }

	UFUNCTION(BlueprintPure, Category = "Enemy Pool")
	bool IsPoolActive() const { return bIsPoolInWorld && !IsDead(); }

	UFUNCTION(BlueprintPure, Category = "Enemy Pool")
	bool IsPoolInWorld() const { return bIsPoolInWorld; }

	UFUNCTION(BlueprintPure, Category = "Enemy Pool")
	bool IsPoolManaged() const;

	/** Configures a deferred-spawned enemy as hidden pool reserve before BeginPlay. */
	void InitializeForPool(ABHEnemyPoolManager* InPoolManager);

	/** Resets one complete enemy life and spawns a fresh AI controller. Authority only. */
	bool ActivateFromPool(const FTransform& SpawnTransform);

	/** Removes AI/presentation and parks this actor as hidden reusable reserve. Authority only. */
	void DeactivateToPoolStorage(const FTransform& StorageTransform);

	/** Server-authoritative reaction entry point. A negative duration uses EnemyConfigDataAsset. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Combat|Enemy")
	void StartStagger(float Duration = -1.0f);

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
	UFUNCTION()
	void OnRep_PoolInWorld();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayBasicAttack(UAnimMontage* AttackMontage, FName MontageSection);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayReaction(UAnimMontage* ReactionMontage);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSetPoolPresentationActive(bool bActive);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSetDeathCollisionEnabled(bool bEnabled);

	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);
	void HandleAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void HandleAttackMontageFailSafe();
	void BeginAttackRecovery();
	void FinishAttackRecovery();
	void FinishStagger();
	void Die();
	void DisableDeathCollision();
	void ConfigureLiveCollision();
	void ApplyEnemyConfigRuntimeSettings();
	void ApplyPoolPresentationState(bool bActive);
	void DestroyCurrentAIController();
	void ResetGameplayStateForPoolActivation();
	void ClearAttackContext();
	bool IsAttackTargetInHitArea() const;
	const FBHEnemyAttackConfig* GetAttackConfig(FName AttackId) const;
	const FBHEnemyAttackConfig* GetDefaultAttackConfig() const;
	const FBHAttackDefinitionRow* GetAttackDefinition(const FBHEnemyAttackConfig& AttackConfig) const;
	const FBHAttackDefinitionRow* GetActiveAttackDefinition() const;
	FName SelectRandomMontageSection(const FBHEnemyAttackConfig& AttackConfig) const;
	void SetCombatState(EBHEnemyCombatState NewState);

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Combat|Enemy", meta = (AllowPrivateAccess = "true"))
	EBHEnemyCombatState CombatState = EBHEnemyCombatState::Chasing;

	/**
	 * Latched on the first physical arrival at a reserved Attack/Wait/Holding/Pending slot.
	 * Slot reshuffles preserve it; losing/changing the target or leaving engagement resets it.
	 */
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "AI|Formation", meta = (AllowPrivateAccess = "true"))
	bool bHasJoinedFormation = false;

	/** Hysteresis-controlled run intent used when a joined enemy falls behind its reserved slot. */
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "AI|Formation", meta = (AllowPrivateAccess = "true"))
	bool bNeedsFormationCatchUp = false;

	/** Replicated gait request; actual displacement still gates animation playback. */
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "AI|Formation", meta = (AllowPrivateAccess = "true"))
	bool bWantsRunLocomotion = false;

	/** True for living enemies and visible corpses; false only while hidden in free storage. */
	UPROPERTY(ReplicatedUsing = OnRep_PoolInWorld, VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy Pool", meta = (AllowPrivateAccess = "true"))
	bool bIsPoolInWorld = true;

	UPROPERTY(Replicated, Transient)
	TObjectPtr<ABHEnemyPoolManager> PoolManager;

	/** Frozen for one attack so a target switch cannot redirect a hit already in progress. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> AttackTarget;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveAttackMontage;

	FName ActiveAttackId = NAME_None;
	FName ActiveAttackMontageSection = NAME_None;

	FTimerHandle AttackRecoveryTimerHandle;
	FTimerHandle AttackMontageFailSafeTimerHandle;
	FTimerHandle StaggerTimerHandle;
	FTimerHandle DeathCollisionTimerHandle;
	bool bLoggedInvalidAttackConfig = false;
	bool bHasAppliedDamageThisAttack = false;
	int32 SuccessfulAttackStartCount = 0;
};
