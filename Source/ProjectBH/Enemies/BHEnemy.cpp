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
	AttackTarget = TargetActor;
	ActiveAttackId = AttackConfig->AttackId;
	ActiveAttackMontage = AttackConfig->Montage;
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

	MulticastPlayBasicAttack(ActiveAttackMontage);
	return true;
}

void ABHEnemy::MulticastPlayBasicAttack_Implementation(UAnimMontage* AttackMontage)
{
	ActiveAttackMontage = AttackMontage;
	const float MontageDuration = PlayAnimMontage(ActiveAttackMontage);
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

	FOnMontageEnded MontageEndedDelegate;
	MontageEndedDelegate.BindUObject(this, &ThisClass::HandleAttackMontageEnded);
	AnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, ActiveAttackMontage);
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

	BeginAttackRecovery();
}

void ABHEnemy::BeginAttackRecovery()
{
	if (!HasAuthority())
	{
		return;
	}

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
	if (!HasAuthority())
	{
		return;
	}

	AttackTarget = nullptr;
	ActiveAttackMontage = nullptr;
	ActiveAttackId = NAME_None;
	bHasAppliedDamageThisAttack = false;
	SetCombatState(EBHEnemyCombatState::Chasing);
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

void ABHEnemy::SetCombatState(EBHEnemyCombatState NewState)
{
	if (HasAuthority())
	{
		CombatState = NewState;
	}
}
