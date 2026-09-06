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
	return GetAliveEnemyCount(EPooledEnemyKind::Normal)
		+ GetAliveEnemyCount(EPooledEnemyKind::Troll);
}

int32 ABHEnemyPoolManager::GetAliveNormalEnemyCount() const
{
	return GetAliveEnemyCount(EPooledEnemyKind::Normal);
}

int32 ABHEnemyPoolManager::GetAliveTrollCount() const
{
	return GetAliveEnemyCount(EPooledEnemyKind::Troll);
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
	if (TrollEnemyClass == EnemyClass)
	{
		UE_LOG(
			LogProjectBH,
			Error,
			TEXT("%s cannot use the same class for Enemy Class and Troll Enemy Class; disabling Troll composition."),
			*GetName());
		TrollEnemyClass = nullptr;
	}

	bTrollSpawningEnabledForSession = bEnableTrollSpawning && TrollEnemyClass != nullptr;
	PoolCapacity = FMath::Max(1, PoolCapacity);
	ActiveEnemyLimit = FMath::Clamp(ActiveEnemyLimit, 1, PoolCapacity);
	NormalEnemiesPerTroll = FMath::Max(1, NormalEnemiesPerTroll);
	PoolEnemies.Reserve(PoolCapacity);
	FreeEnemies.Reserve(PoolCapacity);

	const int32 TrollPoolCapacity = TrollEnemyClass
		? GetDesiredTrollCount(PoolCapacity)
		: 0;
	const int32 NormalPoolCapacity = PoolCapacity - TrollPoolCapacity;
	for (int32 PoolIndex = 0; PoolIndex < PoolCapacity; ++PoolIndex)
	{
		const TSubclassOf<ABHEnemy> PooledClass = PoolIndex < NormalPoolCapacity
			? EnemyClass
			: TrollEnemyClass;
		if (ABHEnemy* Enemy = SpawnPooledEnemy(PoolIndex, PooledClass))
		{
			PoolEnemies.Add(Enemy);
			FreeEnemies.Add(Enemy);
		}
	}

	const int32 InitialActiveCount = FMath::Min(ActiveEnemyLimit, PoolEnemies.Num());
	const int32 InitialTrollCount = TrollEnemyClass
		? GetDesiredTrollCount(InitialActiveCount)
		: 0;
	int32 ActivatedNormalCount = 0;
	int32 ActivatedTrollCount = 0;
	for (int32 ActiveIndex = 0; ActiveIndex < InitialActiveCount; ++ActiveIndex)
	{
		const bool bSpawnTroll = ActivatedTrollCount < InitialTrollCount
			&& ActivatedNormalCount >= (ActivatedTrollCount + 1) * NormalEnemiesPerTroll;
		const EPooledEnemyKind Kind = bSpawnTroll
			? EPooledEnemyKind::Troll
			: EPooledEnemyKind::Normal;
		ABHEnemy* Enemy = AcquireFreeEnemy(Kind);
		if (!ActivateEnemy(Enemy))
		{
			break;
		}
		if (Kind == EPooledEnemyKind::Troll)
		{
			++ActivatedTrollCount;
		}
		else
		{
			++ActivatedNormalCount;
		}
	}

	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s initialized enemy pool. Spawned:%d Alive:%d (Normal:%d Troll:%d) Free:%d ActiveLimit:%d"),
		*GetName(),
		PoolEnemies.Num(),
		GetAliveEnemyCount(),
		GetAliveNormalEnemyCount(),
		GetAliveTrollCount(),
		GetFreeEnemyCount(),
		ActiveEnemyLimit);
}

ABHEnemy* ABHEnemyPoolManager::SpawnPooledEnemy(
	int32 PoolIndex,
	TSubclassOf<ABHEnemy> PooledEnemyClass)
{
	UWorld* World = GetWorld();
	if (!World || !PooledEnemyClass)
	{
		return nullptr;
	}

	const FTransform StorageTransform = GetStorageTransform(PoolIndex);
	ABHEnemy* Enemy = World->SpawnActorDeferred<ABHEnemy>(
		PooledEnemyClass,
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
	const int32 DesiredTrollCount = TrollEnemyClass
		? GetDesiredTrollCount(EffectiveLimit)
		: 0;
	const int32 DesiredNormalCount = GetDesiredNormalCount(EffectiveLimit);
	const int32 MissingNormalCount = FMath::Max(
		0,
		DesiredNormalCount - GetAliveEnemyCount(EPooledEnemyKind::Normal));
	const int32 MissingTrollCount = FMath::Max(
		0,
		DesiredTrollCount - GetAliveEnemyCount(EPooledEnemyKind::Troll));

	auto ReconcileKind = [this](EPooledEnemyKind Kind, int32 MissingCount)
	{
		int32 PendingCount = GetPendingRespawnCount(Kind);
		while (PendingCount < MissingCount)
		{
			FRespawnRequest& Request = PendingRespawns.AddDefaulted_GetRef();
			Request.ReadyTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, RespawnDelay);
			Request.Sequence = NextRespawnSequence++;
			Request.Kind = Kind;
			++PendingCount;
		}

		while (PendingCount > MissingCount)
		{
			int32 NewestIndex = INDEX_NONE;
			uint64 NewestSequence = 0;
			for (int32 Index = 0; Index < PendingRespawns.Num(); ++Index)
			{
				if (PendingRespawns[Index].Kind == Kind
					&& (NewestIndex == INDEX_NONE
						|| PendingRespawns[Index].Sequence > NewestSequence))
				{
					NewestIndex = Index;
					NewestSequence = PendingRespawns[Index].Sequence;
				}
			}
			if (NewestIndex == INDEX_NONE)
			{
				break;
			}
			PendingRespawns.RemoveAtSwap(NewestIndex, 1, EAllowShrinking::No);
			--PendingCount;
		}
	};

	ReconcileKind(EPooledEnemyKind::Normal, MissingNormalCount);
	ReconcileKind(EPooledEnemyKind::Troll, MissingTrollCount);
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

	const EPooledEnemyKind RequestedKind = PendingRespawns[OldestReadyIndex].Kind;
	ABHEnemy* Enemy = AcquireFreeEnemy(RequestedKind);
	const bool bReclaimedCorpse = Enemy == nullptr;
	if (!Enemy)
	{
		Enemy = ReclaimOldestEligibleCorpse(CurrentTime, RequestedKind);
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
		TEXT("%s spawned %s replacement %s from %s. Alive:%d (Normal:%d Troll:%d) Free:%d Corpses:%d Pending:%d"),
		*GetName(),
		RequestedKind == EPooledEnemyKind::Troll ? TEXT("Troll") : TEXT("normal"),
		*Enemy->GetName(),
		bReclaimedCorpse ? TEXT("oldest corpse") : TEXT("free reserve"),
		GetAliveEnemyCount(),
		GetAliveNormalEnemyCount(),
		GetAliveTrollCount(),
		GetFreeEnemyCount(),
		GetCorpseCount(),
		GetPendingRespawnCount());
}

ABHEnemy* ABHEnemyPoolManager::AcquireFreeEnemy(EPooledEnemyKind Kind)
{
	for (int32 Index = FreeEnemies.Num() - 1; Index >= 0; --Index)
	{
		ABHEnemy* Enemy = FreeEnemies[Index].Get();
		if (!IsValid(Enemy))
		{
			FreeEnemies.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}
		if (IsEnemyKind(Enemy, Kind))
		{
			FreeEnemies.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			return Enemy;
		}
	}
	return nullptr;
}

ABHEnemy* ABHEnemyPoolManager::ReclaimOldestEligibleCorpse(
	float CurrentTime,
	EPooledEnemyKind Kind)
{
	int32 OldestIndex = INDEX_NONE;
	uint64 OldestSequence = TNumericLimits<uint64>::Max();
	for (int32 Index = 0; Index < Corpses.Num(); ++Index)
	{
		const FCorpseRecord& Record = Corpses[Index];
		if (!Record.Enemy.IsValid()
			|| !IsEnemyKind(Record.Enemy.Get(), Kind)
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

bool ABHEnemyPoolManager::IsEnemyKind(
	const ABHEnemy* Enemy,
	EPooledEnemyKind Kind) const
{
	if (!IsValid(Enemy))
	{
		return false;
	}
	const bool bIsTroll = TrollEnemyClass && Enemy->IsA(TrollEnemyClass);
	return Kind == EPooledEnemyKind::Troll ? bIsTroll : !bIsTroll;
}

int32 ABHEnemyPoolManager::GetAliveEnemyCount(EPooledEnemyKind Kind) const
{
	int32 Count = 0;
	for (const ABHEnemy* Enemy : PoolEnemies)
	{
		Count += IsEnemyKind(Enemy, Kind) && Enemy->IsPoolActive() ? 1 : 0;
	}
	return Count;
}

int32 ABHEnemyPoolManager::GetPendingRespawnCount(EPooledEnemyKind Kind) const
{
	int32 Count = 0;
	for (const FRespawnRequest& Request : PendingRespawns)
	{
		Count += Request.Kind == Kind ? 1 : 0;
	}
	return Count;
}

int32 ABHEnemyPoolManager::GetDesiredTrollCount(int32 TotalCount) const
{
	if (!bTrollSpawningEnabledForSession || !TrollEnemyClass || TotalCount <= 0)
	{
		return 0;
	}
	const int32 GroupSize = FMath::Max(1, NormalEnemiesPerTroll) + 1;
	return FMath::Max(0, TotalCount) / GroupSize;
}

int32 ABHEnemyPoolManager::GetDesiredNormalCount(int32 TotalCount) const
{
	const int32 SafeTotal = FMath::Max(0, TotalCount);
	return SafeTotal - GetDesiredTrollCount(SafeTotal);
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
		TEXT("Enemy Pool %d/%d | Alive:%d/%d N:%d T:%d Free:%d Corpses:%d Pending:%d | Sum:%d %s"),
		PoolEnemies.Num(),
		PoolCapacity,
		AliveCount,
		FMath::Min(ActiveEnemyLimit, PoolEnemies.Num()),
		GetAliveNormalEnemyCount(),
		GetAliveTrollCount(),
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
