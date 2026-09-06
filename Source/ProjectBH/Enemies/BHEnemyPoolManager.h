// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BHEnemyPoolManager.generated.h"

class ABHEnemy;

/**
 * Server-authoritative fixed Normal/Troll enemy pool with corpse retention.
 *
 * Alive enemies are capped independently from the total pooled actor count.
 * Dead actors stay visible until no hidden free actor remains for a replacement.
 */
UCLASS(Blueprintable)
class PROJECTBH_API ABHEnemyPoolManager : public AActor
{
	GENERATED_BODY()

public:
	ABHEnemyPoolManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Called by a managed enemy after its server-authoritative death transition. */
	void NotifyEnemyDied(ABHEnemy* Enemy);

	UFUNCTION(BlueprintPure, Category = "Enemy Pool")
	int32 GetAliveEnemyCount() const;

	UFUNCTION(BlueprintPure, Category = "Enemy Pool")
	int32 GetAliveNormalEnemyCount() const;

	UFUNCTION(BlueprintPure, Category = "Enemy Pool")
	int32 GetAliveTrollCount() const;

	UFUNCTION(BlueprintPure, Category = "Enemy Pool")
	int32 GetFreeEnemyCount() const;

	UFUNCTION(BlueprintPure, Category = "Enemy Pool")
	int32 GetCorpseCount() const;

	UFUNCTION(BlueprintPure, Category = "Enemy Pool")
	int32 GetPendingRespawnCount() const { return PendingRespawns.Num(); }

protected:
	/** Normal BP_BHEnemy-derived class managed by this pool. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Setup")
	TSubclassOf<ABHEnemy> EnemyClass;

	/** Include Trolls at BeginPlay. Changes apply on the next play session. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Setup")
	bool bEnableTrollSpawning = true;

	/** Optional BP_BHTroll-derived class inserted into the same pool population. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Setup")
	TSubclassOf<ABHEnemy> TrollEnemyClass;

	/** Place one Troll after each run of this many normal enemies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Composition", meta = (ClampMin = "1", UIMin = "1"))
	int32 NormalEnemiesPerTroll = 10;

	/** Total actor count: alive + visible corpses + hidden free reserve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Capacity", meta = (ClampMin = "1", UIMin = "1"))
	int32 PoolCapacity = 40;

	/** Maximum number of living enemies participating in AI, combat, and crowd movement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Capacity", meta = (ClampMin = "1", UIMin = "1"))
	int32 ActiveEnemyLimit = 12;

	/** Time between a death and its replacement becoming eligible to spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Respawn", meta = (ClampMin = "0.0", Units = "s"))
	float RespawnDelay = 5.0f;

	/** A corpse younger than this is never reclaimed, even when the free reserve is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Respawn", meta = (ClampMin = "0.0", Units = "s"))
	float MinimumCorpseDisplayTime = 5.0f;

	/** Level actors used as round-robin spawn transforms. TargetPoint actors are sufficient. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Enemy Pool|Spawn")
	TArray<TObjectPtr<AActor>> SpawnPoints;

	/** If no valid SpawnPoints are assigned, discover TargetPoint actors in the current world. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Spawn")
	bool bAutoDiscoverTargetPointsWhenEmpty = true;

	/** Spread repeated uses around each Spawn Point instead of stacking enemies at one transform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Spawn")
	bool bSpreadRepeatedSpawnPoints = true;

	/** Grid spacing around this manager when no valid Spawn Point is assigned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Spawn", meta = (ClampMin = "0.0", Units = "cm"))
	float FallbackSpawnSpacing = 175.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Spawn")
	FVector SpawnNavProjectionExtent = FVector(100.0f, 100.0f, 300.0f);

	/** Hidden, collision-free actors are parked near the manager to avoid Kill Z/world-bounds removal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool|Storage")
	FVector StorageLocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug|Enemy Pool")
	bool bDrawPoolDebug = true;

private:
	enum class EPooledEnemyKind : uint8
	{
		Normal,
		Troll
	};

	struct FCorpseRecord
	{
		TWeakObjectPtr<ABHEnemy> Enemy;
		float DeathTime = 0.0f;
		uint64 Sequence = 0;
	};

	struct FRespawnRequest
	{
		float ReadyTime = 0.0f;
		uint64 Sequence = 0;
		EPooledEnemyKind Kind = EPooledEnemyKind::Normal;
	};

	void InitializePool();
	void ResolveSpawnPoints();
	ABHEnemy* SpawnPooledEnemy(
		int32 PoolIndex,
		TSubclassOf<ABHEnemy> PooledEnemyClass);
	void ReconcileRespawnDemand();
	void ProcessOneReadyRespawn();
	ABHEnemy* AcquireFreeEnemy(EPooledEnemyKind Kind);
	ABHEnemy* ReclaimOldestEligibleCorpse(float CurrentTime, EPooledEnemyKind Kind);
	bool ActivateEnemy(ABHEnemy* Enemy);
	bool IsEnemyKind(const ABHEnemy* Enemy, EPooledEnemyKind Kind) const;
	int32 GetAliveEnemyCount(EPooledEnemyKind Kind) const;
	int32 GetPendingRespawnCount(EPooledEnemyKind Kind) const;
	int32 GetDesiredTrollCount(int32 TotalCount) const;
	int32 GetDesiredNormalCount(int32 TotalCount) const;
	FTransform GetNextSpawnTransform();
	FTransform GetStorageTransform(int32 PoolIndex) const;
	void PruneInvalidEntries();
	void DrawPoolDebug() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ABHEnemy>> PoolEnemies;

	TArray<TWeakObjectPtr<ABHEnemy>> FreeEnemies;
	TArray<FCorpseRecord> Corpses;
	TArray<FRespawnRequest> PendingRespawns;

	/** Preserve the prewarmed composition throughout this play session. */
	bool bTrollSpawningEnabledForSession = false;
	uint64 NextCorpseSequence = 1;
	uint64 NextRespawnSequence = 1;
	int32 NextSpawnPointIndex = 0;
	int32 NextFallbackSpawnIndex = 0;
};
