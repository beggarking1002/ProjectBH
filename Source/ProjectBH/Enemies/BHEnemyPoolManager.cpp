// Copyright ProjectBH. All Rights Reserved.

#include "BHEnemyPoolManager.h"

#include "BHEnemy.h"
#include "../Debug/BHDebugDraw.h"
#include "../ProjectBH.h"
#include "DrawDebugHelpers.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"

ABHEnemyPoolManager::ABHEnemyPoolManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = true;
	SetReplicateMovement(false);
}

void ABHEnemyPoolManager::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		ResolveSpawnPoints();
		InitializePool();
	}
}

void ABHEnemyPoolManager::ResolveSpawnPoints()
{
	SpawnPoints.RemoveAll(
		[](const TObjectPtr<AActor>& SpawnPoint)
		{
			return !IsValid(SpawnPoint);
		});

	if (!SpawnPoints.IsEmpty() || !bAutoDiscoverTargetPointsWhenEmpty || !GetWorld())
	{
		return;
	}

	for (TActorIterator<ATargetPoint> Iterator(GetWorld()); Iterator; ++Iterator)
	{
		SpawnPoints.Add(*Iterator);
	}

	SpawnPoints.Sort(
		[](const AActor& Left, const AActor& Right)
		{
			return Left.GetName() < Right.GetName();
		});

	if (SpawnPoints.IsEmpty())
	{
		UE_LOG(
			LogProjectBH,
			Warning,
			TEXT("%s has no valid Spawn Points or TargetPoint actors; using its fallback grid."),
			*GetName());
		return;
	}

	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s auto-discovered %d TargetPoint spawn point(s)."),
		*GetName(),
		SpawnPoints.Num());
}

void ABHEnemyPoolManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	PruneInvalidEntries();
	ReconcileRespawnDemand();
	ProcessOneReadyRespawn();
	if (BHDebugDraw::IsPoolEnabled(bDrawPoolDebug))
	{
		DrawPoolDebug();
	}
}

void ABHEnemyPoolManager::NotifyEnemyDied(ABHEnemy* Enemy)
{
	if (!HasAuthority() || !IsValid(Enemy) || !PoolEnemies.Contains(Enemy))
	{
		return;
	}

	for (const FCorpseRecord& Record : Corpses)
	{
		if (Record.Enemy.Get() == Enemy)
		{
			return;
		}
	}

	FCorpseRecord& Record = Corpses.AddDefaulted_GetRef();
	Record.Enemy = Enemy;
	Record.DeathTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	Record.Sequence = NextCorpseSequence++;
	ReconcileRespawnDemand();

	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s registered corpse %s. Alive:%d Free:%d Corpses:%d Pending:%d"),
		*GetName(),
		*Enemy->GetName(),
		GetAliveEnemyCount(),
		GetFreeEnemyCount(),
		GetCorpseCount(),
		GetPendingRespawnCount());
}

int32 ABHEnemyPoolManager::GetAliveEnemyCount() const
{
	int32 Count = 0;
	for (const ABHEnemy* Enemy : PoolEnemies)
	{
		Count += IsValid(Enemy) && Enemy->IsPoolActive() ? 1 : 0;
	}
	return Count;
}

int32 ABHEnemyPoolManager::GetFreeEnemyCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ABHEnemy>& Enemy : FreeEnemies)
	{
		Count += Enemy.IsValid() ? 1 : 0;
	}
	return Count;
}

int32 ABHEnemyPoolManager::GetCorpseCount() const
{
	int32 Count = 0;
	for (const FCorpseRecord& Record : Corpses)
	{
		Count += Record.Enemy.IsValid() ? 1 : 0;
	}
	return Count;
}

void ABHEnemyPoolManager::InitializePool()
{
	if (!EnemyClass)
	{
		UE_LOG(LogProjectBH, Error, TEXT("%s cannot initialize: assign Enemy Class."), *GetName());
		return;
	}

	PoolCapacity = FMath::Max(1, PoolCapacity);
	ActiveEnemyLimit = FMath::Clamp(ActiveEnemyLimit, 1, PoolCapacity);
	PoolEnemies.Reserve(PoolCapacity);
	FreeEnemies.Reserve(PoolCapacity);

	for (int32 PoolIndex = 0; PoolIndex < PoolCapacity; ++PoolIndex)
	{
		if (ABHEnemy* Enemy = SpawnPooledEnemy(PoolIndex))
		{
			PoolEnemies.Add(Enemy);
			FreeEnemies.Add(Enemy);
		}
	}

	const int32 InitialActiveCount = FMath::Min(ActiveEnemyLimit, PoolEnemies.Num());
	for (int32 ActiveIndex = 0; ActiveIndex < InitialActiveCount; ++ActiveIndex)
	{
		ABHEnemy* Enemy = AcquireFreeEnemy();
		if (!ActivateEnemy(Enemy))
		{
			break;
		}
	}

	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s initialized enemy pool. Spawned:%d Alive:%d Free:%d ActiveLimit:%d"),
		*GetName(),
		PoolEnemies.Num(),
		GetAliveEnemyCount(),
		GetFreeEnemyCount(),
		ActiveEnemyLimit);
}

ABHEnemy* ABHEnemyPoolManager::SpawnPooledEnemy(int32 PoolIndex)
{
	UWorld* World = GetWorld();
	if (!World || !EnemyClass)
	{
		return nullptr;
	}

	const FTransform StorageTransform = GetStorageTransform(PoolIndex);
	ABHEnemy* Enemy = World->SpawnActorDeferred<ABHEnemy>(
		EnemyClass,
		StorageTransform,
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Enemy)
	{
		UE_LOG(LogProjectBH, Error, TEXT("%s failed to prewarm pooled enemy %d."), *GetName(), PoolIndex);
		return nullptr;
	}

	Enemy->InitializeForPool(this);
	UGameplayStatics::FinishSpawningActor(Enemy, StorageTransform);
	Enemy->DeactivateToPoolStorage(StorageTransform);
	return Enemy;
}

void ABHEnemyPoolManager::ReconcileRespawnDemand()
{
	if (!GetWorld())
	{
		return;
	}

	const int32 EffectiveLimit = FMath::Min(FMath::Max(1, ActiveEnemyLimit), PoolEnemies.Num());
	const int32 MissingAliveCount = FMath::Max(0, EffectiveLimit - GetAliveEnemyCount());
	while (PendingRespawns.Num() < MissingAliveCount)
	{
		FRespawnRequest& Request = PendingRespawns.AddDefaulted_GetRef();
		Request.ReadyTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, RespawnDelay);
		Request.Sequence = NextRespawnSequence++;
	}

	while (PendingRespawns.Num() > MissingAliveCount)
	{
		int32 NewestIndex = 0;
		for (int32 Index = 1; Index < PendingRespawns.Num(); ++Index)
		{
			if (PendingRespawns[Index].Sequence > PendingRespawns[NewestIndex].Sequence)
			{
				NewestIndex = Index;
			}
		}
		PendingRespawns.RemoveAtSwap(NewestIndex, 1, EAllowShrinking::No);
	}
}

void ABHEnemyPoolManager::ProcessOneReadyRespawn()
{
	UWorld* World = GetWorld();
	if (!World || PendingRespawns.IsEmpty())
	{
		return;
	}

	int32 OldestReadyIndex = INDEX_NONE;
	uint64 OldestSequence = TNumericLimits<uint64>::Max();
	const float CurrentTime = World->GetTimeSeconds();
	for (int32 Index = 0; Index < PendingRespawns.Num(); ++Index)
	{
		const FRespawnRequest& Request = PendingRespawns[Index];
		if (Request.ReadyTime <= CurrentTime && Request.Sequence < OldestSequence)
		{
			OldestSequence = Request.Sequence;
			OldestReadyIndex = Index;
		}
	}
	if (OldestReadyIndex == INDEX_NONE)
	{
		return;
	}

	ABHEnemy* Enemy = AcquireFreeEnemy();
	const bool bReclaimedCorpse = Enemy == nullptr;
	if (!Enemy)
	{
		Enemy = ReclaimOldestEligibleCorpse(CurrentTime);
	}
	if (!Enemy)
	{
		return;
	}

	if (!ActivateEnemy(Enemy))
	{
		if (!FreeEnemies.Contains(Enemy))
		{
			FreeEnemies.Add(Enemy);
		}
		return;
	}

	PendingRespawns.RemoveAtSwap(OldestReadyIndex, 1, EAllowShrinking::No);
	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s spawned replacement %s from %s. Alive:%d Free:%d Corpses:%d Pending:%d"),
		*GetName(),
		*Enemy->GetName(),
		bReclaimedCorpse ? TEXT("oldest corpse") : TEXT("free reserve"),
		GetAliveEnemyCount(),
		GetFreeEnemyCount(),
		GetCorpseCount(),
		GetPendingRespawnCount());
}

ABHEnemy* ABHEnemyPoolManager::AcquireFreeEnemy()
{
	while (!FreeEnemies.IsEmpty())
	{
		const int32 LastIndex = FreeEnemies.Num() - 1;
		ABHEnemy* Enemy = FreeEnemies[LastIndex].Get();
		FreeEnemies.RemoveAt(LastIndex, 1, EAllowShrinking::No);
		if (IsValid(Enemy))
		{
			return Enemy;
		}
	}
	return nullptr;
}

ABHEnemy* ABHEnemyPoolManager::ReclaimOldestEligibleCorpse(float CurrentTime)
{
	int32 OldestIndex = INDEX_NONE;
	uint64 OldestSequence = TNumericLimits<uint64>::Max();
	for (int32 Index = 0; Index < Corpses.Num(); ++Index)
	{
		const FCorpseRecord& Record = Corpses[Index];
		if (!Record.Enemy.IsValid()
			|| CurrentTime - Record.DeathTime < FMath::Max(0.0f, MinimumCorpseDisplayTime)
			|| Record.Sequence >= OldestSequence)
		{
			continue;
		}

		OldestIndex = Index;
		OldestSequence = Record.Sequence;
	}
	if (OldestIndex == INDEX_NONE)
	{
		return nullptr;
	}

	ABHEnemy* Enemy = Corpses[OldestIndex].Enemy.Get();
	Corpses.RemoveAtSwap(OldestIndex, 1, EAllowShrinking::No);
	if (Enemy)
	{
		Enemy->DeactivateToPoolStorage(GetStorageTransform(PoolEnemies.IndexOfByKey(Enemy)));
	}
	return Enemy;
}

bool ABHEnemyPoolManager::ActivateEnemy(ABHEnemy* Enemy)
{
	if (!IsValid(Enemy))
	{
		return false;
	}

	return Enemy->ActivateFromPool(GetNextSpawnTransform());
}

FTransform ABHEnemyPoolManager::GetNextSpawnTransform()
{
	for (int32 Attempt = 0; Attempt < SpawnPoints.Num(); ++Attempt)
	{
		const int32 SpawnSequence = NextSpawnPointIndex++;
		const int32 Index = SpawnSequence % SpawnPoints.Num();
		if (const AActor* SpawnPoint = SpawnPoints[Index])
		{
			FTransform Result = SpawnPoint->GetActorTransform();
			const int32 EffectiveLimit = FMath::Max(1, FMath::Min(ActiveEnemyLimit, PoolEnemies.Num()));
			const int32 UsesPerPoint = FMath::Max(1, FMath::CeilToInt(
				static_cast<float>(EffectiveLimit) / static_cast<float>(SpawnPoints.Num())));
			const int32 ReuseIndex = (SpawnSequence / SpawnPoints.Num()) % UsesPerPoint;
			if (bSpreadRepeatedSpawnPoints && UsesPerPoint > 1)
			{
				const int32 Columns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(UsesPerPoint))));
				const int32 Rows = FMath::CeilToInt(static_cast<float>(UsesPerPoint) / static_cast<float>(Columns));
				const int32 Column = ReuseIndex % Columns;
				const int32 Row = ReuseIndex / Columns;
				const FVector LocalOffset(
					(static_cast<float>(Column) - static_cast<float>(Columns - 1) * 0.5f) * FallbackSpawnSpacing,
					(static_cast<float>(Row) - static_cast<float>(Rows - 1) * 0.5f) * FallbackSpawnSpacing,
					0.0f);
				Result.AddToTranslation(SpawnPoint->GetActorRotation().RotateVector(LocalOffset));
			}
			if (const UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
			{
				FNavLocation ProjectedLocation;
				if (NavigationSystem->ProjectPointToNavigation(Result.GetLocation(), ProjectedLocation, SpawnNavProjectionExtent))
				{
					Result.SetLocation(ProjectedLocation.Location);
				}
			}
			return Result;
		}
	}

	const int32 EffectiveLimit = FMath::Max(1, FMath::Min(ActiveEnemyLimit, PoolEnemies.Num()));
	const int32 Columns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(EffectiveLimit))));
	const int32 GridIndex = NextFallbackSpawnIndex++ % EffectiveLimit;
	const int32 Column = GridIndex % Columns;
	const int32 Row = GridIndex / Columns;
	const int32 Rows = FMath::CeilToInt(static_cast<float>(EffectiveLimit) / static_cast<float>(Columns));
	const FVector LocalOffset(
		(static_cast<float>(Column) - static_cast<float>(Columns - 1) * 0.5f) * FallbackSpawnSpacing,
		(static_cast<float>(Row) - static_cast<float>(Rows - 1) * 0.5f) * FallbackSpawnSpacing,
		0.0f);
	FTransform Result = GetActorTransform();
	Result.SetLocation(GetActorLocation() + GetActorRotation().RotateVector(LocalOffset));

	if (const UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation ProjectedLocation;
		if (NavigationSystem->ProjectPointToNavigation(Result.GetLocation(), ProjectedLocation, SpawnNavProjectionExtent))
		{
			Result.SetLocation(ProjectedLocation.Location);
		}
	}
	return Result;
}

FTransform ABHEnemyPoolManager::GetStorageTransform(int32 PoolIndex) const
{
	FTransform Result = GetActorTransform();
	Result.SetLocation(
		GetActorLocation()
		+ StorageLocationOffset
		+ FVector(0.0f, 0.0f, static_cast<float>(FMath::Max(0, PoolIndex)) * 10.0f));
	return Result;
}

void ABHEnemyPoolManager::PruneInvalidEntries()
{
	PoolEnemies.RemoveAllSwap(
		[](const TObjectPtr<ABHEnemy>& Enemy)
		{
			return !IsValid(Enemy);
		},
		EAllowShrinking::No);
	FreeEnemies.RemoveAllSwap(
		[](const TWeakObjectPtr<ABHEnemy>& Enemy)
		{
			return !Enemy.IsValid();
		},
		EAllowShrinking::No);
	Corpses.RemoveAllSwap(
		[](const FCorpseRecord& Record)
		{
			return !Record.Enemy.IsValid();
		},
		EAllowShrinking::No);
}

void ABHEnemyPoolManager::DrawPoolDebug() const
{
	if (!GetWorld())
	{
		return;
	}

	const int32 AliveCount = GetAliveEnemyCount();
	const int32 FreeCount = GetFreeEnemyCount();
	const int32 CorpseCount = GetCorpseCount();
	const int32 TrackedTotal = AliveCount + FreeCount + CorpseCount;
	const bool bCountsMatch = TrackedTotal == PoolEnemies.Num();
	const FString DebugText = FString::Printf(
		TEXT("Enemy Pool %d/%d | Alive:%d/%d Free:%d Corpses:%d Pending:%d | Sum:%d %s"),
		PoolEnemies.Num(),
		PoolCapacity,
		AliveCount,
		FMath::Min(ActiveEnemyLimit, PoolEnemies.Num()),
		FreeCount,
		CorpseCount,
		GetPendingRespawnCount(),
		TrackedTotal,
		bCountsMatch ? TEXT("OK") : TEXT("MISMATCH"));
	DrawDebugString(
		GetWorld(),
		GetActorLocation() + FVector(0.0f, 0.0f, 140.0f),
		DebugText,
		nullptr,
		bCountsMatch ? FColor::Green : FColor::Red,
		PrimaryActorTick.TickInterval + 0.1f,
		false,
		1.1f);
}
