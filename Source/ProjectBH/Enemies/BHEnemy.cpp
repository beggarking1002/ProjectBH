// Copyright ProjectBH. All Rights Reserved.

#include "BHEnemy.h"

#include "../AI/BHCrowdEnemyAIController.h"
#include "../AbilitySystem/BHAbilitySystemComponent.h"
#include "../AbilitySystem/BHAttributeSet.h"
#include "../AbilitySystem/GameplayEffects/BHGE_EnemyBasicAttackDamage.h"
#include "../BHGameplayTags.h"
#include "../Combat/BHAttackDefinition.h"
#include "../DataAssets/Enemy/DataAsset_EnemyConfig.h"
#include "../ProjectBH.h"
#include "AbilitySystemInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
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

}

void ABHEnemy::BeginPlay()
{
	Super::BeginPlay();

	if (EnemyConfigDataAsset)
	{
		GetCharacterMovement()->MaxWalkSpeed = EnemyConfigDataAsset->MaxWalkSpeed;
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

void ABHEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABHEnemy, CombatState);
}

bool ABHEnemy::TryStartBasicAttack(AActor* TargetActor)
{
	if (!HasAuthority() || IsAttackLocked() || !IsValid(TargetActor))
	{
		return false;
	}

	const FBHEnemyAttackConfig* AttackConfig = GetDefaultAttackConfig();
	const FBHAttackDefinitionRow* AttackDefinition = AttackConfig ? GetAttackDefinition(*AttackConfig) : nullptr;
	if (!AttackConfig || !AttackDefinition || !AttackConfig->Montage)
	{
		if (!bLoggedInvalidAttackConfig)
		{
			UE_LOG(LogProjectBH, Warning, TEXT("%s cannot attack. Assign EnemyConfigDataAsset, its default attack, a valid AttackDefinition row, and Montage."), *GetName());
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

	bLoggedInvalidAttackConfig = false;
	GetWorldTimerManager().ClearTimer(AttackRecoveryTimerHandle);
	GetWorldTimerManager().ClearTimer(AttackMontageFailSafeTimerHandle);
	AttackTarget = TargetActor;
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

	MulticastPlayBasicAttack(ActiveAttackMontage, ActiveAttackMontageSection);
	return true;
}

void ABHEnemy::MulticastPlayBasicAttack_Implementation(UAnimMontage* AttackMontage, FName MontageSection)
{
	ActiveAttackMontage = AttackMontage;
	ActiveAttackMontageSection = MontageSection;
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

	GetWorldTimerManager().ClearTimer(AttackRecoveryTimerHandle);
	GetWorldTimerManager().ClearTimer(AttackMontageFailSafeTimerHandle);
	GetWorldTimerManager().ClearTimer(StaggerTimerHandle);
	ClearAttackContext();
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
	BeginAttackRecovery();
}

void ABHEnemy::HandleAttackMontageFailSafe()
{
	if (!HasAuthority() || CombatState != EBHEnemyCombatState::Attacking)
	{
		return;
	}

	UE_LOG(LogProjectBH, Warning, TEXT("%s attack montage did not finish in time; forcing recovery."), *GetName());
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

	GetWorldTimerManager().ClearTimer(AttackRecoveryTimerHandle);
	GetWorldTimerManager().ClearTimer(AttackMontageFailSafeTimerHandle);
	GetWorldTimerManager().ClearTimer(StaggerTimerHandle);
	ClearAttackContext();
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
	if (GetCapsuleComponent())
	{
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void ABHEnemy::ClearAttackContext()
{
	AttackTarget = nullptr;
	ActiveAttackMontage = nullptr;
	ActiveAttackId = NAME_None;
	ActiveAttackMontageSection = NAME_None;
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
	return FVector::DotProduct(GetActorForwardVector(), TargetDirection2D) >= MinimumFacingDot;
}

float ABHEnemy::GetAttackStartRange() const
{
	const FBHEnemyAttackConfig* AttackConfig = GetDefaultAttackConfig();
	const FBHAttackDefinitionRow* AttackDefinition = AttackConfig ? GetAttackDefinition(*AttackConfig) : nullptr;
	return AttackDefinition ? AttackDefinition->AttackStartRange : 150.0f;
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
