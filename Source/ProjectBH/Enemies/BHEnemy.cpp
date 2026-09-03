// Copyright ProjectBH. All Rights Reserved.

#include "BHEnemy.h"

#include "BHEnemyPoolManager.h"
#include "../AI/BHCrowdEnemyAIController.h"
#include "../AbilitySystem/BHAbilitySystemComponent.h"
#include "../AbilitySystem/BHAttributeSet.h"
#include "../AbilitySystem/GameplayEffects/BHGE_EnemyBasicAttackDamage.h"
#include "../BHGameplayTags.h"
#include "../BHCollisionChannels.h"
#include "../Combat/BHAttackDefinition.h"
#include "../Components/Combat/BHEnemyChargeComponent.h"
#include "../DataAssets/Enemy/DataAsset_EnemyConfig.h"
#include "../ProjectBH.h"
#include "AbilitySystemInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ABHEnemy::ABHEnemy()
{
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = ABHCrowdEnemyAIController::StaticClass();

	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = true;
	ConfigureLiveCollision();

	EquippedWeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EquippedWeaponMesh"));
	EquippedWeaponMesh->SetupAttachment(GetMesh(), TEXT("WeaponSocket_R"));
	EquippedWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EquippedWeaponMesh->SetGenerateOverlapEvents(false);
	ChargeComponent = CreateDefaultSubobject<UBHEnemyChargeComponent>(TEXT("ChargeComponent"));

}

void ABHEnemy::BeginPlay()
{
	Super::BeginPlay();
	ConfigureLiveCollision();
	ApplyEnemyConfigRuntimeSettings();
	if (HasAuthority() && (!IsPoolManaged() || bIsPoolInWorld))
	{
		SelectAndEquipRandomWeapon();
	}
	else if (!HasAuthority())
	{
		ApplySelectedWeapon();
	}

	if (HasAuthority() && GetBHAbilitySystemComponent())
	{
		GetBHAbilitySystemComponent()
			->GetGameplayAttributeValueChangeDelegate(UBHAttributeSet::GetHealthAttribute())
			.AddUObject(this, &ThisClass::HandleHealthChanged);

		if (GetBHAbilitySystemComponent()->GetNumericAttribute(UBHAttributeSet::GetHealthAttribute()) <= 0.0f)
		{
			Die();
		}
	}

	if (HasAuthority() && IsPoolManaged() && !bIsPoolInWorld)
	{
		ApplyPoolPresentationState(false);
		DestroyCurrentAIController();
	}
}

void ABHEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || !bWalkLeftForAnimationPreview)
	{
		return;
	}

	GetCharacterMovement()->MaxWalkSpeed = AnimationPreviewWalkSpeed;
	AddMovementInput(-GetActorRightVector(), 1.0f, true);
}

void ABHEnemy::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (!HasAuthority() || !bHighGroundDropActive)
	{
		return;
	}

	bHighGroundDropActive = false;
	ForceNetUpdate();
	if (ABHCrowdEnemyAIController* CrowdController = Cast<ABHCrowdEnemyAIController>(GetController()))
	{
		CrowdController->NotifyCombatSlotAssignmentChanged();
	}
}

void ABHEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABHEnemy, CombatState);
	DOREPLIFETIME(ABHEnemy, ActiveAttackPresentationMode);
	DOREPLIFETIME(ABHEnemy, bHasJoinedFormation);
	DOREPLIFETIME(ABHEnemy, bNeedsFormationCatchUp);
	DOREPLIFETIME(ABHEnemy, bWantsRunLocomotion);
	DOREPLIFETIME(ABHEnemy, bHighGroundDropActive);
	DOREPLIFETIME(ABHEnemy, bIsPoolInWorld);
	DOREPLIFETIME(ABHEnemy, PoolManager);
	DOREPLIFETIME(ABHEnemy, SelectedWeaponIndex);
}

void ABHEnemy::MarkFormationJoined()
{
	if (HasAuthority())
	{
		bHasJoinedFormation = true;
	}
}

void ABHEnemy::ResetFormationJoinState()
{
	if (HasAuthority())
	{
		bHasJoinedFormation = false;
		bNeedsFormationCatchUp = false;
		bWantsRunLocomotion = false;
	}
}

void ABHEnemy::SetFormationCatchUpRequired(bool bRequired)
{
	if (HasAuthority())
	{
		bNeedsFormationCatchUp = bRequired;
	}
}

void ABHEnemy::SetWantsRunLocomotion(bool bWantsRun)
{
	if (HasAuthority())
	{
		bWantsRunLocomotion = bWantsRun;
	}
}

bool ABHEnemy::TryStartHighGroundDrop(
	const FVector& LandingLocation,
	float LaunchZ,
	float MaxHorizontalSpeed)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!HasAuthority()
		|| CombatState != EBHEnemyCombatState::Chasing
		|| IsAttackLocked()
		|| bHighGroundDropActive
		|| !Movement
		|| !Movement->IsMovingOnGround())
	{
		return false;
	}

	const FVector Delta = LandingLocation - GetActorLocation();
	const FVector HorizontalDelta(Delta.X, Delta.Y, 0.0f);
	const float HorizontalDistance = HorizontalDelta.Size2D();
	const float GravityMagnitude = FMath::Abs(Movement->GetGravityZ());
	if (HorizontalDistance <= UE_SMALL_NUMBER
		|| Delta.Z >= 0.0f
		|| GravityMagnitude <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const float SafeLaunchZ = FMath::Max(0.0f, LaunchZ);
	const float FallDistance = -Delta.Z;
	const float NaturalFlightTime = (SafeLaunchZ
		+ FMath::Sqrt(FMath::Square(SafeLaunchZ) + 2.0f * GravityMagnitude * FallDistance))
		/ GravityMagnitude;
	const float FlightTime = FMath::Max(
		FMath::Max(0.1f, NaturalFlightTime),
		HorizontalDistance / FMath::Max(1.0f, MaxHorizontalSpeed));
	const FVector HorizontalVelocity = HorizontalDelta / FlightTime;
	const float VerticalVelocity = (Delta.Z
		+ 0.5f * GravityMagnitude * FMath::Square(FlightTime))
		/ FlightTime;

	bHighGroundDropActive = true;
	bNeedsFormationCatchUp = false;
	bWantsRunLocomotion = false;
	// Mark the drop active before aborting path following. Any synchronous move
	// completion callback can then recognize that this is an intentional launch.
	if (ABHCrowdEnemyAIController* CrowdController = Cast<ABHCrowdEnemyAIController>(GetController()))
	{
		CrowdController->StopMovement();
	}
	const FVector FacingDirection = HorizontalDelta.GetSafeNormal2D();
	if (!FacingDirection.IsNearlyZero())
	{
		const FRotator FacingRotation(0.0f, FacingDirection.Rotation().Yaw, 0.0f);
		SetActorRotation(FacingRotation);
		if (Controller)
		{
			Controller->SetControlRotation(FacingRotation);
		}
	}
	ForceNetUpdate();
	LaunchCharacter(
		FVector(HorizontalVelocity.X, HorizontalVelocity.Y, VerticalVelocity),
		true,
		true);
	return true;
}

bool ABHEnemy::IsPoolManaged() const
{
	return IsValid(PoolManager.Get());
}

void ABHEnemy::InitializeForPool(ABHEnemyPoolManager* InPoolManager)
{
	if (!HasAuthority() || !IsValid(InPoolManager))
	{
		return;
	}

	PoolManager = InPoolManager;
	bIsPoolInWorld = false;
	AutoPossessAI = EAutoPossessAI::Disabled;
	SetLifeSpan(0.0f);
}

bool ABHEnemy::ActivateFromPool(const FTransform& SpawnTransform)
{
	if (!HasAuthority() || !IsPoolManaged())
	{
		return false;
	}

	GetWorldTimerManager().ClearAllTimersForObject(this);
	SetLifeSpan(0.0f);
	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
	ResetGameplayStateForPoolActivation();
	ApplyEnemyConfigRuntimeSettings();
	SelectAndEquipRandomWeapon();
	SetCombatState(EBHEnemyCombatState::Chasing);
	bIsPoolInWorld = true;
	ApplyPoolPresentationState(true);
	MulticastSetPoolPresentationActive(true);
	MulticastSetDeathCollisionEnabled(true);

	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	if (!EnemyConfigDataAsset)
	{
		// Preserve the legacy pool fallback for deliberately unconfigured test enemies.
		GetCharacterMovement()->MaxWalkSpeed = 300.0f;
	}
	SpawnDefaultController();
	if (!Controller)
	{
		UE_LOG(LogProjectBH, Error, TEXT("%s pool activation failed to spawn its AI Controller."), *GetName());
		DeactivateToPoolStorage(GetActorTransform());
		return false;
	}

	ForceNetUpdate();
	return true;
}

void ABHEnemy::DeactivateToPoolStorage(const FTransform& StorageTransform)
{
	if (!HasAuthority() || !IsPoolManaged())
	{
		return;
	}

	if (ChargeComponent)
	{
		ChargeComponent->ResetChargeState();
	}
	GetWorldTimerManager().ClearAllTimersForObject(this);
	ClearAttackContext();
	SetLifeSpan(0.0f);
	DestroyCurrentAIController();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	SetCombatState(EBHEnemyCombatState::Dead);
	bIsPoolInWorld = false;
	SetActorTransform(StorageTransform, false, nullptr, ETeleportType::TeleportPhysics);
	ApplyPoolPresentationState(false);
	MulticastSetPoolPresentationActive(false);
	ForceNetUpdate();
}

void ABHEnemy::OnRep_PoolInWorld()
{
	ApplyPoolPresentationState(bIsPoolInWorld);
}

void ABHEnemy::OnRep_SelectedWeaponIndex()
{
	ApplySelectedWeapon();
}

bool ABHEnemy::CanMoveDuringAttack() const
{
	return UsesMovingUpperBodyAttack()
		&& (CombatState == EBHEnemyCombatState::Attacking
			|| CombatState == EBHEnemyCombatState::Recovering);
}

bool ABHEnemy::UsesMovingUpperBodyAttack() const
{
	return ActiveAttackPresentationMode == EBHEnemyAttackPresentationMode::MovingUpperBody;
}

bool ABHEnemy::TryStartBasicAttack(
	AActor* TargetActor,
	EBHEnemyAttackPresentationMode PresentationMode)
{
	const FBHEnemyAttackConfig* AttackConfig = GetDefaultAttackConfig();
	return AttackConfig
		&& TryStartConfiguredAttack(AttackConfig->AttackId, TargetActor, PresentationMode);
}

bool ABHEnemy::TryStartChargeAttack(AActor* TargetActor)
{
	return ChargeComponent && ChargeComponent->TryStartCharge(TargetActor);
}

bool ABHEnemy::IsChargeAttackActive() const
{
	return ChargeComponent && ChargeComponent->IsChargeActive();
}

bool ABHEnemy::TryStartConfiguredAttack(
	FName AttackId,
	AActor* TargetActor,
	EBHEnemyAttackPresentationMode PresentationMode)
{
	if (!HasAuthority() || IsAttackLocked() || !IsValid(TargetActor) || AttackId.IsNone())
	{
		return false;
	}

	const FBHEnemyAttackConfig* AttackConfig = GetAttackConfig(AttackId);
	const FBHAttackDefinitionRow* AttackDefinition = AttackConfig ? GetAttackDefinition(*AttackConfig) : nullptr;
	if (!AttackConfig || !AttackDefinition || !AttackConfig->Montage)
	{
		if (!bLoggedInvalidAttackConfig)
		{
			UE_LOG(LogProjectBH, Warning, TEXT("%s cannot use attack '%s'. Assign its AttackDefinition row and Montage."), *GetName(), *AttackId.ToString());
			bLoggedInvalidAttackConfig = true;
		}
		return false;
	}

	if (AttackDefinition->HitDetectionMode != EBHAttackHitDetectionMode::CommittedTargetCone)
	{
		if (!bLoggedInvalidAttackConfig)
		{
			UE_LOG(LogProjectBH, Warning, TEXT("%s default attack requests a hit detection mode that is not implemented yet."), *GetName());
			bLoggedInvalidAttackConfig = true;
		}
		return false;
	}
	if (PresentationMode == EBHEnemyAttackPresentationMode::MovingUpperBody
		&& (!AttackDefinition->bAllowMovingAttack
			|| FVector::DistSquared2D(GetActorLocation(), TargetActor->GetActorLocation())
				> FMath::Square(FMath::Max(0.0f, AttackDefinition->MovingAttackStartRange))))
	{
		return false;
	}

	bLoggedInvalidAttackConfig = false;
	GetWorldTimerManager().ClearTimer(AttackRecoveryTimerHandle);
	GetWorldTimerManager().ClearTimer(AttackMontageFailSafeTimerHandle);
	AttackTarget = TargetActor;
	ActiveAttackPresentationMode = PresentationMode;
	ActiveAttackId = AttackConfig->AttackId;
	ActiveAttackMontage = AttackConfig->Montage;
	ActiveAttackMontageSection = SelectRandomMontageSection(*AttackConfig);
	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s selected montage section '%s' for attack '%s'."),
		*GetName(),
		*ActiveAttackMontageSection.ToString(),
		*ActiveAttackId.ToString());
	bHasAppliedDamageThisAttack = false;
	SetCombatState(EBHEnemyCombatState::Attacking);

	const FVector ToTarget = TargetActor->GetActorLocation() - GetActorLocation();
	if (!ToTarget.IsNearlyZero())
	{
		const FRotator AttackRotation(0.0f, ToTarget.Rotation().Yaw, 0.0f);
		SetActorRotation(AttackRotation);
		if (Controller)
		{
			Controller->SetControlRotation(AttackRotation);
		}
	}

	MulticastPlayAttack(
		ActiveAttackMontage,
		ActiveAttackMontageSection,
		ActiveAttackPresentationMode);
	return true;
}

void ABHEnemy::MulticastPlayAttack_Implementation(
	UAnimMontage* AttackMontage,
	FName MontageSection,
	EBHEnemyAttackPresentationMode PresentationMode)
{
	ActiveAttackMontage = AttackMontage;
	ActiveAttackMontageSection = MontageSection;
	ActiveAttackPresentationMode = PresentationMode;
	const float MontageDuration = PlayAnimMontage(ActiveAttackMontage, 1.0f, ActiveAttackMontageSection);
	if (!HasAuthority())
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (MontageDuration <= 0.0f || !AnimInstance)
	{
		UE_LOG(LogProjectBH, Warning, TEXT("%s failed to play its configured enemy attack montage."), *GetName());
		BeginAttackRecovery();
		return;
	}
	++SuccessfulAttackStartCount;

	FOnMontageEnded MontageEndedDelegate;
	MontageEndedDelegate.BindUObject(this, &ThisClass::HandleAttackMontageEnded);
	AnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, ActiveAttackMontage);

	const float FailSafeGrace = EnemyConfigDataAsset
		? EnemyConfigDataAsset->AttackMontageFailSafeGrace
		: 0.5f;
	GetWorldTimerManager().SetTimer(
		AttackMontageFailSafeTimerHandle,
		this,
		&ThisClass::HandleAttackMontageFailSafe,
		MontageDuration + FailSafeGrace,
		false);
}

void ABHEnemy::MulticastPlayReaction_Implementation(UAnimMontage* ReactionMontage)
{
	StopAnimMontage();
	if (ReactionMontage)
	{
		PlayAnimMontage(ReactionMontage);
	}
}

void ABHEnemy::MulticastSetPoolPresentationActive_Implementation(bool bActive)
{
	if (bActive)
	{
		StopAnimMontage();
	}
	ApplyPoolPresentationState(bActive);
}

void ABHEnemy::MulticastSetDeathCollisionEnabled_Implementation(bool bEnabled)
{
	if (GetCapsuleComponent())
	{
		GetCapsuleComponent()->SetCollisionEnabled(
			bEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
}

void ABHEnemy::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	if (!HasAuthority() || IsDead() || ChangeData.NewValue >= ChangeData.OldValue)
	{
		return;
	}

	if (ChangeData.NewValue <= 0.0f)
	{
		Die();
		return;
	}

	StartStagger();
}

void ABHEnemy::StartStagger(float Duration)
{
	if (!HasAuthority() || IsDead())
	{
		return;
	}

	if (ChargeComponent)
	{
		ChargeComponent->CancelCharge();
	}
	GetWorldTimerManager().ClearTimer(AttackRecoveryTimerHandle);
	GetWorldTimerManager().ClearTimer(AttackMontageFailSafeTimerHandle);
	GetWorldTimerManager().ClearTimer(StaggerTimerHandle);
	ClearAttackContext();
	bHighGroundDropActive = false;
	SetCombatState(EBHEnemyCombatState::Staggered);

	if (ABHCrowdEnemyAIController* CrowdController = Cast<ABHCrowdEnemyAIController>(GetController()))
	{
		CrowdController->ReleaseCombatSlot(EBHCombatSlotReleaseReason::Staggered);
		CrowdController->StopMovement();
	}
	GetCharacterMovement()->StopMovementImmediately();

	MulticastPlayReaction(EnemyConfigDataAsset ? EnemyConfigDataAsset->HitReactMontage : nullptr);
	float ResolvedDuration = Duration >= 0.0f
		? Duration
		: (EnemyConfigDataAsset ? EnemyConfigDataAsset->StaggerDuration : 0.6f);
	if (Duration < 0.0f && EnemyConfigDataAsset && EnemyConfigDataAsset->HitReactMontage)
	{
		ResolvedDuration = FMath::Max(
			ResolvedDuration,
			EnemyConfigDataAsset->HitReactMontage->GetPlayLength());
	}
	if (ResolvedDuration <= 0.0f)
	{
		FinishStagger();
		return;
	}

	GetWorldTimerManager().SetTimer(
		StaggerTimerHandle,
		this,
		&ThisClass::FinishStagger,
		ResolvedDuration,
		false);
}

void ABHEnemy::PerformBasicAttackHit()
{
	if (!HasAuthority() || CombatState != EBHEnemyCombatState::Attacking || bHasAppliedDamageThisAttack || !IsValid(AttackTarget))
	{
		return;
	}

	const FBHEnemyAttackConfig* AttackConfig = GetAttackConfig(ActiveAttackId);
	const FBHAttackDefinitionRow* AttackDefinition = AttackConfig ? GetAttackDefinition(*AttackConfig) : nullptr;
	if (!AttackConfig || !AttackDefinition)
	{
		return;
	}

	if (!IsAttackTargetInHitArea())
	{
		UE_LOG(LogProjectBH, Verbose, TEXT("%s attack missed %s."), *GetName(), *AttackTarget->GetName());
		return;
	}

	UBHAbilitySystemComponent* SourceAbilitySystem = GetBHAbilitySystemComponent();
	IAbilitySystemInterface* TargetAbilitySystemInterface = Cast<IAbilitySystemInterface>(AttackTarget);
	UAbilitySystemComponent* TargetAbilitySystem = TargetAbilitySystemInterface ? TargetAbilitySystemInterface->GetAbilitySystemComponent() : nullptr;
	if (!SourceAbilitySystem || !TargetAbilitySystem || TargetAbilitySystem == SourceAbilitySystem)
	{
		return;
	}

	FGameplayEffectContextHandle EffectContext = SourceAbilitySystem->MakeEffectContext();
	EffectContext.AddSourceObject(this);
	TSubclassOf<UGameplayEffect> DamageEffect = AttackConfig->DamageEffect;
	if (!DamageEffect)
	{
		DamageEffect = UBHGE_EnemyBasicAttackDamage::StaticClass();
	}
	const FGameplayEffectSpecHandle EffectSpec = SourceAbilitySystem->MakeOutgoingSpec(DamageEffect, 1.0f, EffectContext);
	if (EffectSpec.IsValid())
	{
		EffectSpec.Data->SetSetByCallerMagnitude(BHGameplayTags::Data_Damage, -AttackDefinition->BaseDamage);
		const float PreviousHealth = TargetAbilitySystem->GetNumericAttribute(UBHAttributeSet::GetHealthAttribute());
		SourceAbilitySystem->ApplyGameplayEffectSpecToTarget(*EffectSpec.Data.Get(), TargetAbilitySystem);
		const float CurrentHealth = TargetAbilitySystem->GetNumericAttribute(UBHAttributeSet::GetHealthAttribute());
		bHasAppliedDamageThisAttack = true;
		UE_LOG(LogProjectBH, Display, TEXT("%s hit %s. Health: %.1f -> %.1f"), *GetName(), *AttackTarget->GetName(), PreviousHealth, CurrentHealth);
	}
}

void ABHEnemy::HandleAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (!HasAuthority() || Montage != ActiveAttackMontage || CombatState != EBHEnemyCombatState::Attacking)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(AttackMontageFailSafeTimerHandle);
	if (ChargeComponent)
	{
		ChargeComponent->NotifyOwningAttackEnded(Montage);
	}
	BeginAttackRecovery();
}

void ABHEnemy::HandleAttackMontageFailSafe()
{
	if (!HasAuthority() || CombatState != EBHEnemyCombatState::Attacking)
	{
		return;
	}

	UE_LOG(LogProjectBH, Warning, TEXT("%s attack montage did not finish in time; forcing recovery."), *GetName());
	if (ChargeComponent)
	{
		ChargeComponent->CancelCharge();
	}
	MulticastPlayReaction(nullptr);
	BeginAttackRecovery();
}

void ABHEnemy::BeginAttackRecovery()
{
	if (!HasAuthority())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(AttackMontageFailSafeTimerHandle);
	SetCombatState(EBHEnemyCombatState::Recovering);
	const FBHAttackDefinitionRow* AttackDefinition = GetActiveAttackDefinition();
	const float RecoveryDuration = AttackDefinition ? AttackDefinition->RecoveryDuration : 0.0f;
	if (RecoveryDuration <= 0.0f)
	{
		FinishAttackRecovery();
		return;
	}

	GetWorldTimerManager().SetTimer(
		AttackRecoveryTimerHandle,
		this,
		&ThisClass::FinishAttackRecovery,
		RecoveryDuration,
		false);
}

void ABHEnemy::FinishAttackRecovery()
{
	if (!HasAuthority() || CombatState != EBHEnemyCombatState::Recovering)
	{
		return;
	}

	ClearAttackContext();
	SetCombatState(EBHEnemyCombatState::Chasing);
}

void ABHEnemy::FinishStagger()
{
	if (!HasAuthority() || CombatState != EBHEnemyCombatState::Staggered)
	{
		return;
	}

	MulticastPlayReaction(nullptr);
	SetCombatState(EBHEnemyCombatState::Chasing);
}

void ABHEnemy::Die()
{
	if (!HasAuthority() || IsDead())
	{
		return;
	}

	if (ChargeComponent)
	{
		ChargeComponent->CancelCharge();
	}
	GetWorldTimerManager().ClearTimer(AttackRecoveryTimerHandle);
	GetWorldTimerManager().ClearTimer(AttackMontageFailSafeTimerHandle);
	GetWorldTimerManager().ClearTimer(StaggerTimerHandle);
	ClearAttackContext();
	bHighGroundDropActive = false;
	SetCombatState(EBHEnemyCombatState::Dead);

	if (ABHCrowdEnemyAIController* CrowdController = Cast<ABHCrowdEnemyAIController>(GetController()))
	{
		CrowdController->ReleaseCombatSlot(EBHCombatSlotReleaseReason::Died);
		CrowdController->StopMovement();
	}
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();

	MulticastPlayReaction(EnemyConfigDataAsset ? EnemyConfigDataAsset->DeathMontage : nullptr);

	const float CollisionDisableDelay = EnemyConfigDataAsset
		? EnemyConfigDataAsset->DeathCollisionDisableDelay
		: 0.2f;
	if (CollisionDisableDelay <= 0.0f)
	{
		DisableDeathCollision();
	}
	else
	{
		GetWorldTimerManager().SetTimer(
			DeathCollisionTimerHandle,
			this,
			&ThisClass::DisableDeathCollision,
			CollisionDisableDelay,
			false);
	}

	if (IsPoolManaged())
	{
		SetLifeSpan(0.0f);
		DestroyCurrentAIController();
		PoolManager->NotifyEnemyDied(this);
		return;
	}

	const float DeadActorLifeSpan = EnemyConfigDataAsset
		? EnemyConfigDataAsset->DeadActorLifeSpan
		: 5.0f;
	if (DeadActorLifeSpan > 0.0f)
	{
		SetLifeSpan(DeadActorLifeSpan);
	}
	DetachFromControllerPendingDestroy();
}

void ABHEnemy::DisableDeathCollision()
{
	MulticastSetDeathCollisionEnabled(false);
}

void ABHEnemy::ConfigureLiveCollision()
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}

	Capsule->SetCollisionObjectType(BHCollisionChannels::EnemyPawn);
	Capsule->SetCollisionResponseToChannel(BHCollisionChannels::EnemyPawn, ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	// Enemy traffic is handled by Detour Crowd and the local overlap recovery.
	// Do not let a live enemy carve Recast and make another enemy's static lane
	// query (including Troll Charge) interpret crowd traffic as level geometry.
	Capsule->SetCanEverAffectNavigation(false);
}

void ABHEnemy::ApplyEnemyConfigRuntimeSettings()
{
	if (!EnemyConfigDataAsset)
	{
		return;
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (Movement)
	{
		Movement->MaxWalkSpeed = FMath::Max(0.0f, EnemyConfigDataAsset->MaxWalkSpeed);
	}

	if (!Capsule)
	{
		return;
	}

	const float ConfiguredRadius = EnemyConfigDataAsset->CapsuleRadiusOverride;
	const float ConfiguredHalfHeight = EnemyConfigDataAsset->CapsuleHalfHeightOverride;
	if (ConfiguredRadius <= 0.0f && ConfiguredHalfHeight <= 0.0f)
	{
		return;
	}

	const float CapsuleRadius = ConfiguredRadius > 0.0f
		? ConfiguredRadius
		: Capsule->GetUnscaledCapsuleRadius();
	const float CapsuleHalfHeight = FMath::Max(
		CapsuleRadius,
		ConfiguredHalfHeight > 0.0f
			? ConfiguredHalfHeight
			: Capsule->GetUnscaledCapsuleHalfHeight());
	Capsule->SetCapsuleSize(CapsuleRadius, CapsuleHalfHeight, true);
	if (Movement)
	{
		// Keep Detour/path-following agent dimensions synchronized with the body
		// configured by the Enemy Data Asset.
		Movement->UpdateNavAgent(*Capsule);
	}
}

void ABHEnemy::ApplyPoolPresentationState(bool bActive)
{
	SetActorHiddenInGame(!bActive);
	SetActorEnableCollision(bActive);
	SetActorTickEnabled(bActive);
	if (GetMesh())
	{
		GetMesh()->SetComponentTickEnabled(bActive);
	}
	if (GetCapsuleComponent())
	{
		if (bActive)
		{
			ConfigureLiveCollision();
		}
		GetCapsuleComponent()->SetCollisionEnabled(
			bActive ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
}

void ABHEnemy::DestroyCurrentAIController()
{
	AController* ExistingController = GetController();
	if (!ExistingController)
	{
		return;
	}

	if (ABHCrowdEnemyAIController* CrowdController = Cast<ABHCrowdEnemyAIController>(ExistingController))
	{
		CrowdController->ReleaseCombatSlot(EBHCombatSlotReleaseReason::UnPossessed);
		CrowdController->StopMovement();
	}
	ExistingController->UnPossess();
	ExistingController->Destroy();
}

void ABHEnemy::ResetGameplayStateForPoolActivation()
{
	if (ChargeComponent)
	{
		ChargeComponent->ResetChargeState();
	}
	ClearAttackContext();
	ResetFormationJoinState();
	bHighGroundDropActive = false;
	bLoggedInvalidAttackConfig = false;
	bHasAppliedDamageThisAttack = false;
	SuccessfulAttackStartCount = 0;

	UBHAbilitySystemComponent* AbilitySystem = GetBHAbilitySystemComponent();
	if (!AbilitySystem)
	{
		return;
	}

	AbilitySystem->RemoveActiveEffects(FGameplayEffectQuery());
	float MaxHealth = AbilitySystem->GetNumericAttribute(UBHAttributeSet::GetMaxHealthAttribute());
	if (MaxHealth <= 0.0f)
	{
		MaxHealth = 100.0f;
		AbilitySystem->SetNumericAttributeBase(UBHAttributeSet::GetMaxHealthAttribute(), MaxHealth);
	}
	AbilitySystem->SetNumericAttributeBase(UBHAttributeSet::GetHealthAttribute(), MaxHealth);
}

void ABHEnemy::SelectAndEquipRandomWeapon()
{
	if (!HasAuthority())
	{
		return;
	}

	SelectedWeaponIndex = INDEX_NONE;
	if (EnemyConfigDataAsset)
	{
		float TotalWeight = 0.0f;
		for (const FBHEnemyWeaponOption& Option : EnemyConfigDataAsset->WeaponOptions)
		{
			if (IsValid(Option.WeaponMesh))
			{
				TotalWeight += FMath::Max(0.0f, Option.SelectionWeight);
			}
		}

		if (TotalWeight > UE_SMALL_NUMBER)
		{
			float RemainingWeight = FMath::FRandRange(0.0f, TotalWeight);
			for (int32 OptionIndex = 0; OptionIndex < EnemyConfigDataAsset->WeaponOptions.Num(); ++OptionIndex)
			{
				const FBHEnemyWeaponOption& Option = EnemyConfigDataAsset->WeaponOptions[OptionIndex];
				if (!IsValid(Option.WeaponMesh) || Option.SelectionWeight <= 0.0f)
				{
					continue;
				}

				SelectedWeaponIndex = OptionIndex;
				RemainingWeight -= Option.SelectionWeight;
				if (RemainingWeight <= 0.0f)
				{
					break;
				}
			}
		}
	}

	ApplySelectedWeapon();
	ForceNetUpdate();
}

void ABHEnemy::ApplySelectedWeapon()
{
	if (!EquippedWeaponMesh)
	{
		return;
	}

	EquippedWeaponMesh->SetStaticMesh(nullptr);
	EquippedWeaponMesh->SetRelativeTransform(FTransform::Identity);
	if (!EnemyConfigDataAsset
		|| !EnemyConfigDataAsset->WeaponOptions.IsValidIndex(SelectedWeaponIndex))
	{
		return;
	}

	const FBHEnemyWeaponOption& Option = EnemyConfigDataAsset->WeaponOptions[SelectedWeaponIndex];
	if (!IsValid(Option.WeaponMesh) || Option.SelectionWeight <= 0.0f)
	{
		return;
	}

	const FName SocketName = EnemyConfigDataAsset->WeaponSocketName.IsNone()
		? FName(TEXT("WeaponSocket_R"))
		: EnemyConfigDataAsset->WeaponSocketName;
	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		EquippedWeaponMesh->AttachToComponent(
			CharacterMesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			SocketName);
	}
	EquippedWeaponMesh->SetStaticMesh(Option.WeaponMesh);
	EquippedWeaponMesh->SetRelativeTransform(Option.RelativeTransform);
}

void ABHEnemy::ClearAttackContext()
{
	AttackTarget = nullptr;
	ActiveAttackMontage = nullptr;
	ActiveAttackId = NAME_None;
	ActiveAttackMontageSection = NAME_None;
	ActiveAttackPresentationMode = EBHEnemyAttackPresentationMode::StationaryFullBody;
	bHasAppliedDamageThisAttack = false;
}

bool ABHEnemy::IsAttackTargetInHitArea() const
{
	if (!IsValid(AttackTarget))
	{
		return false;
	}

	const FBHAttackDefinitionRow* AttackDefinition = GetActiveAttackDefinition();
	if (!AttackDefinition || AttackDefinition->HitDetectionMode != EBHAttackHitDetectionMode::CommittedTargetCone)
	{
		return false;
	}

	const FVector ToTarget = AttackTarget->GetActorLocation() - GetActorLocation();
	if (FMath::Abs(ToTarget.Z) > AttackDefinition->TargetConeHeightTolerance || ToTarget.SizeSquared2D() > FMath::Square(AttackDefinition->TargetConeRange))
	{
		return false;
	}

	const FVector TargetDirection2D = ToTarget.GetSafeNormal2D();
	if (TargetDirection2D.IsNearlyZero())
	{
		return true;
	}

	const float MinimumFacingDot = FMath::Cos(FMath::DegreesToRadians(AttackDefinition->TargetConeHalfAngle));
	const FVector AttackFacing = ChargeComponent && ChargeComponent->IsChargeActive()
		? ChargeComponent->GetChargeDirection()
		: GetActorForwardVector();
	return FVector::DotProduct(AttackFacing, TargetDirection2D) >= MinimumFacingDot;
}

float ABHEnemy::GetAttackStartRange() const
{
	const FBHEnemyAttackConfig* AttackConfig = GetDefaultAttackConfig();
	const FBHAttackDefinitionRow* AttackDefinition = AttackConfig ? GetAttackDefinition(*AttackConfig) : nullptr;
	return AttackDefinition ? AttackDefinition->AttackStartRange : 150.0f;
}

float ABHEnemy::GetMovingAttackStartRange() const
{
	const FBHEnemyAttackConfig* AttackConfig = GetDefaultAttackConfig();
	const FBHAttackDefinitionRow* AttackDefinition = AttackConfig ? GetAttackDefinition(*AttackConfig) : nullptr;
	return AttackDefinition
		? FMath::Max(0.0f, AttackDefinition->MovingAttackStartRange)
		: 0.0f;
}

bool ABHEnemy::IsMovingAttackEnabled() const
{
	const FBHEnemyAttackConfig* AttackConfig = GetDefaultAttackConfig();
	const FBHAttackDefinitionRow* AttackDefinition = AttackConfig ? GetAttackDefinition(*AttackConfig) : nullptr;
	return AttackDefinition && AttackDefinition->bAllowMovingAttack;
}

EBHEnemySizeClass ABHEnemy::GetEnemySizeClass() const
{
	return EnemyConfigDataAsset
		? EnemyConfigDataAsset->SizeClass
		: EBHEnemySizeClass::Normal;
}

int32 ABHEnemy::GetAttackSlotCost() const
{
	return EnemyConfigDataAsset
		? FMath::Max(1, EnemyConfigDataAsset->AttackSlotCost)
		: 1;
}

float ABHEnemy::GetAttackSlotExclusionRadius() const
{
	return EnemyConfigDataAsset
		? FMath::Max(0.0f, EnemyConfigDataAsset->AttackSlotExclusionRadius)
		: 0.0f;
}

int32 ABHEnemy::GetMaxConcurrentAttackersOfSize() const
{
	return EnemyConfigDataAsset
		? FMath::Max(0, EnemyConfigDataAsset->MaxConcurrentAttackersOfSize)
		: 0;
}

const FBHEnemyAttackConfig* ABHEnemy::GetAttackConfig(FName AttackId) const
{
	return EnemyConfigDataAsset ? EnemyConfigDataAsset->FindAttackById(AttackId) : nullptr;
}

const FBHEnemyAttackConfig* ABHEnemy::GetDefaultAttackConfig() const
{
	return EnemyConfigDataAsset ? EnemyConfigDataAsset->FindDefaultAttack() : nullptr;
}

const FBHAttackDefinitionRow* ABHEnemy::GetAttackDefinition(const FBHEnemyAttackConfig& AttackConfig) const
{
	if (!AttackConfig.AttackDefinition.DataTable || AttackConfig.AttackDefinition.RowName.IsNone())
	{
		return nullptr;
	}

	static const FString ContextString(TEXT("BHEnemy Attack Definition"));
	return AttackConfig.AttackDefinition.DataTable->FindRow<FBHAttackDefinitionRow>(
		AttackConfig.AttackDefinition.RowName,
		ContextString,
		false);
}

const FBHAttackDefinitionRow* ABHEnemy::GetActiveAttackDefinition() const
{
	const FBHEnemyAttackConfig* AttackConfig = GetAttackConfig(ActiveAttackId);
	return AttackConfig ? GetAttackDefinition(*AttackConfig) : nullptr;
}

FName ABHEnemy::SelectRandomMontageSection(const FBHEnemyAttackConfig& AttackConfig) const
{
	if (!AttackConfig.Montage || AttackConfig.MontageSections.IsEmpty())
	{
		return NAME_None;
	}

	TArray<FName, TInlineAllocator<8>> ValidSections;
	for (const FName SectionName : AttackConfig.MontageSections)
	{
		if (!SectionName.IsNone() && AttackConfig.Montage->IsValidSectionName(SectionName))
		{
			ValidSections.Add(SectionName);
		}
		else
		{
			UE_LOG(LogProjectBH, Warning, TEXT("%s ignored invalid montage section '%s' in attack '%s'."), *GetName(), *SectionName.ToString(), *AttackConfig.AttackId.ToString());
		}
	}

	return ValidSections.IsEmpty()
		? NAME_None
		: ValidSections[FMath::RandHelper(ValidSections.Num())];
}

void ABHEnemy::SetCombatState(EBHEnemyCombatState NewState)
{
	if (!HasAuthority() || CombatState == NewState)
	{
		return;
	}

	const EBHEnemyCombatState PreviousState = CombatState;
	CombatState = NewState;
	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s combat state: %s -> %s"),
		*GetName(),
		*StaticEnum<EBHEnemyCombatState>()->GetNameStringByValue(static_cast<int64>(PreviousState)),
		*StaticEnum<EBHEnemyCombatState>()->GetNameStringByValue(static_cast<int64>(NewState)));
}
