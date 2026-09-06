// Copyright ProjectBH. All Rights Reserved.

#include "BHEnemyChargeComponent.h"
#include "BHChargePrediction.h"

#include "../../AI/BHCrowdEnemyAIController.h"
#include "../../BHCollisionChannels.h"
#include "../../DataAssets/Enemy/DataAsset_EnemyConfig.h"
#include "../../Enemies/BHEnemy.h"
#include "../../ProjectBH.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"

UBHEnemyChargeComponent::UBHEnemyChargeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UBHEnemyChargeComponent::BeginPlay()
{
	Super::BeginPlay();

	OwningEnemy = Cast<ABHEnemy>(GetOwner());
	if (OwningEnemy && OwningEnemy->GetCharacterMovement())
	{
		AddTickPrerequisiteComponent(OwningEnemy->GetCharacterMovement());
	}
	SetComponentTickEnabled(false);
}

void UBHEnemyChargeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FinishCharge(false);
	Super::EndPlay(EndPlayReason);
}

void UBHEnemyChargeComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bChargeActive || !OwningEnemy || !OwningEnemy->HasAuthority())
	{
		return;
	}

	const FBHEnemyChargeConfig* Config = GetChargeConfig();
	if (!Config
		|| OwningEnemy->GetCombatState() != EBHEnemyCombatState::Attacking
		|| OwningEnemy->ActiveAttackId != Config->AttackId)
	{
		FinishCharge(true);
		return;
	}

	const FVector CurrentLocation = OwningEnemy->GetActorLocation();
	// Include stationary wind-up frames: an enemy may enter an unmoving capsule.
	SweepAsideEnemies(PreviousChargeLocation, CurrentLocation, *Config);
	PreviousChargeLocation = CurrentLocation;
}

bool UBHEnemyChargeComponent::TryStartCharge(AActor* TargetActor)
{
	const FBHEnemyChargeConfig* Config = GetChargeConfig();
	UWorld* World = GetWorld();
	if (!Config
		|| bChargeActive
		|| !Config->bEnabled
		|| !World
		|| !OwningEnemy
		|| !OwningEnemy->HasAuthority()
		|| OwningEnemy->GetEnemySizeClass() != EBHEnemySizeClass::Large
		|| OwningEnemy->GetCombatState() != EBHEnemyCombatState::Chasing
		|| !OwningEnemy->IsPoolActive()
		|| !IsValid(TargetActor)
		|| !OwningEnemy->GetCharacterMovement()
		|| !OwningEnemy->GetCharacterMovement()->IsMovingOnGround()
		|| OwningEnemy->IsHighGroundDropActive()
		|| World->GetTimeSeconds() < NextChargeAllowedTime)
	{
		return false;
	}

	const FBHEnemyAttackConfig* AttackConfig = OwningEnemy->GetAttackConfig(Config->AttackId);
	// Cheap range gate before extracting montage motion or querying the world.
	const float CurrentDistance = FVector::Dist2D(OwningEnemy->GetActorLocation(), TargetActor->GetActorLocation());
	if (CurrentDistance < FMath::Max(0.0f, Config->MinimumStartDistance)
		|| CurrentDistance > FMath::Max(Config->MinimumStartDistance, Config->MaximumStartDistance))
	{
		return false;
	}
	if (!AttackConfig || !AttackConfig->Montage || !AttackConfig->MontageSections.IsEmpty())
	{
		if (AttackConfig && !AttackConfig->MontageSections.IsEmpty() && !bLoggedInvalidRootMotion)
		{
			UE_LOG(
				LogProjectBH,
				Warning,
				TEXT("%s charge attack '%s' must use one Montage with an empty MontageSections array."),
				*OwningEnemy->GetName(),
				*Config->AttackId.ToString());
			bLoggedInvalidRootMotion = true;
		}
		return false;
	}
	float AuthoredTravelDistance = 0.0f;
	float AuthoredTravelYawOffset = 0.0f;
	float AuthoredMaximumLateralDrift = 0.0f;
	float AuthoredYaw = 0.0f;
	if (!GetAuthoredTravelDistance(
		AttackConfig->Montage,
		*Config,
		AuthoredTravelDistance,
		AuthoredTravelYawOffset,
		AuthoredMaximumLateralDrift,
		AuthoredYaw))
	{
		if (!bLoggedInvalidRootMotion)
		{
			UE_LOG(
				LogProjectBH,
				Warning,
				TEXT("%s charge Montage '%s' rejected: horizontal %.1f cm, lateral %.1f/%.1f cm, yaw %.1f/%.1f deg."),
				*OwningEnemy->GetName(),
				*AttackConfig->Montage->GetName(),
				AuthoredTravelDistance,
				AuthoredMaximumLateralDrift,
				FMath::Max(0.0f, Config->MaximumRootMotionLateralDrift),
				AuthoredYaw,
				FMath::Max(0.0f, Config->MaximumRootMotionYaw));
			bLoggedInvalidRootMotion = true;
		}
		return false;
	}
	bLoggedInvalidRootMotion = false;

	FVector ResolvedDirection;
	FVector ResolvedEndLocation;
	if (!ResolveClearChargeLane(
		TargetActor,
		*Config,
		AuthoredTravelDistance,
		AttackConfig->Montage->GetPlayLength() / FMath::Max(0.01f, AttackConfig->Montage->RateScale),
		ResolvedDirection,
		ResolvedEndLocation))
	{
		return false;
	}

	ChargeDirection = ResolvedDirection;
	PreviousChargeLocation = OwningEnemy->GetActorLocation();
	ChargeMontage = AttackConfig->Montage;
	KnockedAsideEnemies.Reset();
	bChargeActive = true;
	// Mark active before aborting the old move; its callback may refresh AI.
	if (ABHCrowdEnemyAIController* CrowdController =
		Cast<ABHCrowdEnemyAIController>(OwningEnemy->GetController()))
	{
		CrowdController->StopMovement();
		CrowdController->ClearFocus(EAIFocusPriority::Gameplay);
	}

	if (UCapsuleComponent* Capsule = OwningEnemy->GetCapsuleComponent())
	{
		PreviousPawnCollisionResponse = Capsule->GetCollisionResponseToChannel(ECC_Pawn);
		// The Root Motion may pass through the committed target. World geometry
		// remains blocking, while other enemies already use the EnemyPawn channel.
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}
	if (UCharacterMovementComponent* Movement = OwningEnemy->GetCharacterMovement())
	{
		bPreviousOrientRotationToMovement = Movement->bOrientRotationToMovement;
		bPreviousUseControllerDesiredRotation = Movement->bUseControllerDesiredRotation;
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = false;
	}

	SetComponentTickEnabled(true);
	if (!OwningEnemy->TryStartConfiguredAttack(
		Config->AttackId,
		TargetActor,
		EBHEnemyAttackPresentationMode::StationaryFullBody)
		|| OwningEnemy->GetCombatState() != EBHEnemyCombatState::Attacking)
	{
		FinishCharge(false);
		return false;
	}

	// Root Motion can be authored on any horizontal root axis. Align its actual
	// world-space travel vector to the committed lane instead of assuming local +X.
	const FRotator ChargeRotation(
		0.0f,
		ChargeDirection.Rotation().Yaw - AuthoredTravelYawOffset,
		0.0f);
	OwningEnemy->SetActorRotation(ChargeRotation);
	if (OwningEnemy->GetController())
	{
		OwningEnemy->GetController()->SetControlRotation(ChargeRotation);
	}

	SweepAsideEnemies(PreviousChargeLocation, PreviousChargeLocation, *Config);
	return true;
}

void UBHEnemyChargeComponent::NotifyOwningAttackEnded(UAnimMontage* EndedMontage)
{
	if (bChargeActive && (!ChargeMontage.IsValid() || ChargeMontage.Get() == EndedMontage))
	{
		FinishCharge(true);
	}
}

void UBHEnemyChargeComponent::CancelCharge(bool bStartCooldown)
{
	if (bChargeActive)
	{
		FinishCharge(bStartCooldown);
	}
}

void UBHEnemyChargeComponent::ResetChargeState()
{
	FinishCharge(false);
	NextChargeAllowedTime = 0.0f;
	bLoggedInvalidRootMotion = false;
}

const FBHEnemyChargeConfig* UBHEnemyChargeComponent::GetChargeConfig() const
{
	return OwningEnemy && OwningEnemy->EnemyConfigDataAsset
		? &OwningEnemy->EnemyConfigDataAsset->Charge
		: nullptr;
}

bool UBHEnemyChargeComponent::ResolveClearChargeLane(
	AActor* TargetActor,
	const FBHEnemyChargeConfig& Config,
	float AuthoredTravelDistance,
	float AuthoredDuration,
	FVector& OutDirection,
	FVector& OutEndLocation) const
{
	OutDirection = FVector::ZeroVector;
	OutEndLocation = FVector::ZeroVector;
	UWorld* World = GetWorld();
	const UCapsuleComponent* Capsule = OwningEnemy ? OwningEnemy->GetCapsuleComponent() : nullptr;
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	if (!World || !Capsule || !NavigationSystem || !IsValid(TargetActor))
	{
		return false;
	}

	const FVector StartLocation = OwningEnemy->GetActorLocation();
	FVector ToTarget = TargetActor->GetActorLocation() - StartLocation;
	if (FMath::Abs(ToTarget.Z) > FMath::Max(0.0f, Config.MaximumTargetHeightDifference))
	{
		return false;
	}

	ToTarget.Z = 0.0f;
	const float TargetDistance = ToTarget.Size2D();
	const float MinimumDistance = FMath::Max(0.0f, Config.MinimumStartDistance);
	const float MaximumDistance = FMath::Max(MinimumDistance, Config.MaximumStartDistance);
	const float TravelDistance = FMath::Max(1.0f, AuthoredTravelDistance);
	if (TargetDistance < MinimumDistance
		|| TargetDistance > MaximumDistance
		|| TargetDistance > TravelDistance + FMath::Max(0.0f, Config.TargetReachTolerance))
	{
		return false;
	}

	ToTarget += BHChargePrediction::PredictOffset(
		ToTarget, TargetActor->GetVelocity(), TravelDistance / FMath::Max(0.01f, AuthoredDuration),
		Config.MaximumPredictionTime, Config.MaximumPredictionDistance,
		TravelDistance + FMath::Max(0.0f, Config.TargetReachTolerance));
	OutDirection = ToTarget.GetSafeNormal2D();
	if (OutDirection.IsNearlyZero())
	{
		return false;
	}
	OutEndLocation = StartLocation + OutDirection * TravelDistance;

	FNavLocation ProjectedStart;
	FNavLocation ProjectedEnd;
	if (!NavigationSystem->ProjectPointToNavigation(
			StartLocation,
			ProjectedStart,
			Config.NavProjectionExtent)
		|| !NavigationSystem->ProjectPointToNavigation(
			OutEndLocation,
			ProjectedEnd,
			Config.NavProjectionExtent)
		|| FVector::DistSquared2D(OutEndLocation, ProjectedEnd.Location)
			> FMath::Square(FMath::Max(0.0f, Config.DestinationProjectionTolerance)))
	{
		return false;
	}

	FVector NavigationHit;
	if (NavigationSystem->NavigationRaycast(
		World,
		ProjectedStart.Location,
		ProjectedEnd.Location,
		NavigationHit))
	{
		return false;
	}

	FCollisionObjectQueryParams ObstacleObjectTypes;
	ObstacleObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObstacleObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	if (!Config.bKnockAsideLargeEnemies)
	{
		// A Large body that cannot be launched is a real obstruction, not ghost traffic.
		ObstacleObjectTypes.AddObjectTypesToQuery(BHCollisionChannels::EnemyPawn);
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BHEnemyChargeLane), false, OwningEnemy);
	QueryParams.AddIgnoredActor(TargetActor);
	// Normal enemies are movable traffic. Large enemies remain blocking unless
	// this charge is explicitly configured to launch them too.
	for (TActorIterator<ABHEnemy> It(World); It; ++It)
	{
		const ABHEnemy* OtherEnemy = *It;
		if (OtherEnemy == OwningEnemy
			|| Config.bKnockAsideLargeEnemies
			|| OtherEnemy->GetEnemySizeClass() != EBHEnemySizeClass::Large)
		{
			QueryParams.AddIgnoredActor(OtherEnemy);
		}
	}

	FHitResult BlockingHit;
	const float BodyRadius = Capsule->GetScaledCapsuleRadius();
	const float BodyHalfHeight = FMath::Max(
		BodyRadius,
		Capsule->GetScaledCapsuleHalfHeight() - 2.0f);
	if (World->SweepSingleByObjectType(
		BlockingHit,
		StartLocation,
		OutEndLocation,
		FQuat::Identity,
		ObstacleObjectTypes,
		FCollisionShape::MakeCapsule(BodyRadius, BodyHalfHeight),
		QueryParams))
	{
		return false;
	}

	const float HorizontalPadding = FMath::Max(0.0f, Config.PathClearancePadding);
	return HorizontalPadding <= 0.0f
		|| !World->SweepSingleByObjectType(
			BlockingHit,
			StartLocation,
			OutEndLocation,
			FQuat::Identity,
			ObstacleObjectTypes,
			FCollisionShape::MakeSphere(BodyRadius + HorizontalPadding),
			QueryParams);
}

bool UBHEnemyChargeComponent::GetAuthoredTravelDistance(
	UAnimMontage* Montage,
	const FBHEnemyChargeConfig& Config,
	float& OutDistance,
	float& OutTravelYawOffset,
	float& OutMaximumLateralDrift,
	float& OutAuthoredYaw) const
{
	OutDistance = 0.0f;
	OutTravelYawOffset = 0.0f;
	OutMaximumLateralDrift = 0.0f;
	OutAuthoredYaw = 0.0f;
	USkeletalMeshComponent* Mesh = OwningEnemy ? OwningEnemy->GetMesh() : nullptr;
	if (!Montage || !Mesh || !Montage->HasRootMotion() || Montage->GetPlayLength() <= 0.0f)
	{
		return false;
	}

	const FTransform RootMotion = Montage->ExtractRootMotionFromTrackRange(
		0.0f,
		Montage->GetPlayLength(),
		FAnimExtractContext());
	const FVector Translation = RootMotion.GetTranslation();
	const FVector AuthoredDirection = Translation.GetSafeNormal2D();
	if (AuthoredDirection.IsNearlyZero())
	{
		return false;
	}

	// The asset may use X, -X, Y, or -Y as its authored forward axis. What matters
	// for a straight charge is deviation from its own net horizontal travel line.
	const FVector AuthoredRight(-AuthoredDirection.Y, AuthoredDirection.X, 0.0f);
	constexpr int32 TrajectorySampleCount = 8;
	for (int32 SampleIndex = 1; SampleIndex < TrajectorySampleCount; ++SampleIndex)
	{
		const float SampleTime = Montage->GetPlayLength()
			* static_cast<float>(SampleIndex)
			/ static_cast<float>(TrajectorySampleCount);
		const FTransform SampleRootMotion = Montage->ExtractRootMotionFromTrackRange(
			0.0f,
			SampleTime,
			FAnimExtractContext());
		OutMaximumLateralDrift = FMath::Max(
			OutMaximumLateralDrift,
			FMath::Abs(FVector::DotProduct(SampleRootMotion.GetTranslation(), AuthoredRight)));
	}

	OutAuthoredYaw = FMath::Abs(FMath::UnwindDegrees(RootMotion.Rotator().Yaw));
	const FTransform WorldRootMotion = Mesh->ConvertLocalRootMotionToWorld(RootMotion);
	const FVector WorldTranslation = WorldRootMotion.GetTranslation();
	OutDistance = WorldTranslation.Size2D();
	if (!FMath::IsFinite(OutDistance)
		|| OutDistance <= UE_KINDA_SMALL_NUMBER
		|| OutMaximumLateralDrift > FMath::Max(0.0f, Config.MaximumRootMotionLateralDrift)
		|| OutAuthoredYaw > FMath::Max(0.0f, Config.MaximumRootMotionYaw))
	{
		return false;
	}

	OutTravelYawOffset = FMath::FindDeltaAngleDegrees(
		OwningEnemy->GetActorRotation().Yaw,
		WorldTranslation.Rotation().Yaw);
	return FMath::IsFinite(OutTravelYawOffset);
}

void UBHEnemyChargeComponent::SweepAsideEnemies(
	const FVector& SweepStart,
	const FVector& SweepEnd,
	const FBHEnemyChargeConfig& Config)
{
	UWorld* World = GetWorld();
	const UCapsuleComponent* Capsule = OwningEnemy ? OwningEnemy->GetCapsuleComponent() : nullptr;
	if (!World || !Capsule)
	{
		return;
	}

	FCollisionObjectQueryParams EnemyObjectType;
	EnemyObjectType.AddObjectTypesToQuery(BHCollisionChannels::EnemyPawn);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BHEnemyChargeKnockAside), false, OwningEnemy);
	TArray<FHitResult> Hits;
	const float SweepRadius = Capsule->GetScaledCapsuleRadius()
		+ FMath::Max(0.0f, Config.KnockAsideRadiusPadding);
	const FCollisionShape ContactShape = FCollisionShape::MakeCapsule(
		SweepRadius, FMath::Max(SweepRadius, Capsule->GetScaledCapsuleHalfHeight()));
	World->SweepMultiByObjectType(
		Hits,
		SweepStart,
		SweepEnd,
		FQuat::Identity,
		EnemyObjectType,
		ContactShape,
		QueryParams);

	// Explicit overlaps cover initial penetration and zero-length movement.
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, SweepEnd, FQuat::Identity,
		EnemyObjectType, ContactShape, QueryParams);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		Hits.Emplace(Overlap.GetActor(), Overlap.GetComponent(), SweepEnd, FVector::ZeroVector);
	}

	const FVector ChargeRight(-ChargeDirection.Y, ChargeDirection.X, 0.0f);
	for (const FHitResult& Hit : Hits)
	{
		ABHEnemy* OtherEnemy = Cast<ABHEnemy>(Hit.GetActor());
		if (!OtherEnemy
			|| OtherEnemy == OwningEnemy
			|| !OtherEnemy->IsPoolActive()
			|| KnockedAsideEnemies.Contains(OtherEnemy)
			|| (!Config.bKnockAsideLargeEnemies
				&& OtherEnemy->GetEnemySizeClass() == EBHEnemySizeClass::Large))
		{
			continue;
		}

		const FVector ToOther = OtherEnemy->GetActorLocation() - OwningEnemy->GetActorLocation();
		const float SideDot = FVector::DotProduct(ToOther, ChargeRight);
		const float SideSign = FMath::Abs(SideDot) > UE_KINDA_SMALL_NUMBER
			? FMath::Sign(SideDot)
			: (OtherEnemy->GetUniqueID() % 2 == 0 ? 1.0f : -1.0f);
		const FVector LaunchVelocity = ChargeRight
				* SideSign * FMath::Max(0.0f, Config.KnockAsideHorizontalSpeed)
			+ ChargeDirection * FMath::Max(0.0f, Config.KnockAsideForwardSpeed)
			+ FVector::UpVector * FMath::Max(0.0f, Config.KnockAsideVerticalSpeed);

		KnockedAsideEnemies.Add(OtherEnemy);
		OtherEnemy->StartStagger(FMath::Max(0.0f, Config.KnockAsideStaggerDuration));
		OtherEnemy->LaunchCharacter(LaunchVelocity, true, true);
	}
}

void UBHEnemyChargeComponent::FinishCharge(bool bStartCooldown)
{
	const bool bWasChargeActive = bChargeActive;
	if (bWasChargeActive && bStartCooldown && GetWorld())
	{
		const FBHEnemyChargeConfig* Config = GetChargeConfig();
		NextChargeAllowedTime = GetWorld()->GetTimeSeconds()
			+ (Config ? FMath::Max(0.0f, Config->Cooldown) : 0.0f);
	}

	if (bWasChargeActive && OwningEnemy)
	{
		if (UCapsuleComponent* Capsule = OwningEnemy->GetCapsuleComponent())
		{
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, PreviousPawnCollisionResponse);
		}
		if (UCharacterMovementComponent* Movement = OwningEnemy->GetCharacterMovement())
		{
			Movement->bOrientRotationToMovement = bPreviousOrientRotationToMovement;
			Movement->bUseControllerDesiredRotation = bPreviousUseControllerDesiredRotation;
		}
	}

	bChargeActive = false;
	ChargeMontage.Reset();
	KnockedAsideEnemies.Reset();
	ChargeDirection = FVector::ZeroVector;
	PreviousChargeLocation = FVector::ZeroVector;
	SetComponentTickEnabled(false);
}
