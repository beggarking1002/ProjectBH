// Copyright ProjectBH. All Rights Reserved.

#include "BHHeroCharacter.h"

#include "BHGameplayTags.h"
#include "AbilitySystem/BHAbilitySystemComponent.h"
#include "AbilitySystem/GameplayEffects/BHGE_BasicAttackDamage.h"
#include "ProjectBH.h"
#include "AbilitySystemInterface.h"
#include "Animation/AnimMontage.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/Combat/CombatEngagementSlotComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/Input/BHInputComponent.h"
#include "GameplayEffect.h"

ABHHeroCharacter::ABHHeroCharacter()
{
	AutoPossessAI = EAutoPossessAI::Disabled;
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Keep the character facing the controller direction so the locomotion Blend Space can play strafe animations.
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = 400.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.0f;

	CombatEngagementSlots = CreateDefaultSubobject<UCombatEngagementSlotComponent>(TEXT("CombatEngagementSlots"));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->TargetArmLength = 200.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 55.0f, 65.0f);
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	ComboAttackSectionNames = { TEXT("Attack_A"), TEXT("Attack_B"), TEXT("Attack_C") };
	BasicAttackDamageEffect = UBHGE_BasicAttackDamage::StaticClass();
}

void ABHHeroCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	checkf(InputConfigDataAsset,TEXT("Forgot to assign a valid data asset as input config"));

	ULocalPlayer* LocalPlayer = GetController<APlayerController>()->GetLocalPlayer();

	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);

	check(Subsystem);

	Subsystem->AddMappingContext(InputConfigDataAsset->DefaultMappingContext,0);

	UBHInputComponent* WarriorInputComponent = CastChecked<UBHInputComponent>(PlayerInputComponent);

	WarriorInputComponent->BindNativeInputAction(InputConfigDataAsset,BHGameplayTags::InputTag_Move,ETriggerEvent::Triggered,this,&ThisClass::Input_Move);
	WarriorInputComponent->BindNativeInputAction(InputConfigDataAsset,BHGameplayTags::InputTag_Look,ETriggerEvent::Triggered,this,&ThisClass::Input_Look);
	WarriorInputComponent->BindNativeInputAction(InputConfigDataAsset, BHGameplayTags::InputTag_BasicAttack, ETriggerEvent::Started, this, &ThisClass::Input_BasicAttack);
}

void ABHHeroCharacter::Input_Move(const FInputActionValue& InputActionValue)
{
	const FVector2D MovementVector = InputActionValue.Get<FVector2D>();

	const FRotator MovementRotation(0.f,Controller->GetControlRotation().Yaw,0.f);

	if (MovementVector.Y != 0.f)
	{
		const FVector ForwardDirection = MovementRotation.RotateVector(FVector::ForwardVector);

		AddMovementInput(ForwardDirection,MovementVector.Y);
	}

	if (MovementVector.X != 0.f)
	{
		const FVector RightDirection = MovementRotation.RotateVector(FVector::RightVector);

		AddMovementInput(RightDirection,MovementVector.X);
	}
}

void ABHHeroCharacter::Input_Look(const FInputActionValue& InputActionValue)
{
	const FVector2D LookAxisVector = InputActionValue.Get<FVector2D>();
	
	if (LookAxisVector.X != 0.f)
	{
		AddControllerYawInput(LookAxisVector.X);
	}

	if (LookAxisVector.Y != 0.f)
	{
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void ABHHeroCharacter::Input_BasicAttack()
{
	if (HasAuthority())
	{
		TryStartBasicAttack();
		return;
	}

	ServerRequestBasicAttack();
}

void ABHHeroCharacter::ServerRequestBasicAttack_Implementation()
{
	TryStartBasicAttack();
}

void ABHHeroCharacter::TryStartBasicAttack()
{
	if (!HasAuthority() || !BasicAttackMontage || ComboAttackSectionNames.IsEmpty())
	{
		return;
	}

	if (bComboActive)
	{
		if (CurrentComboAttackIndex + 1 < ComboAttackSectionNames.Num())
		{
			bComboInputBuffered = true;
		}
		return;
	}

	bComboActive = true;
	bComboInputBuffered = false;
	CurrentComboAttackIndex = 0;
	MulticastPlayBasicAttackSection(ComboAttackSectionNames[CurrentComboAttackIndex]);
}

void ABHHeroCharacter::MulticastPlayBasicAttackSection_Implementation(FName SectionName)
{
	if (BasicAttackMontage && !SectionName.IsNone())
	{
		PlayAnimMontage(BasicAttackMontage, 1.0f, SectionName);
	}
}

void ABHHeroCharacter::PerformBasicAttackHit()
{
	BeginMeleeHitWindow();
	UpdateMeleeHitWindow();
	EndMeleeHitWindow();
}

void ABHHeroCharacter::BeginMeleeHitWindow()
{
	if (!HasAuthority() || !GetMesh())
	{
		return;
	}

	if (!IsValidSwordTracePoint(SwordTraceBaseName) || !IsValidSwordTracePoint(SwordTraceTipName))
	{
		UE_LOG(LogProjectBH, Error, TEXT("%s requires valid sword trace points '%s' and '%s' on its skeletal mesh."), *GetName(), *SwordTraceBaseName.ToString(), *SwordTraceTipName.ToString());
		return;
	}

	PreviousSwordTraceBase = GetMesh()->GetSocketLocation(SwordTraceBaseName);
	PreviousSwordTraceTip = GetMesh()->GetSocketLocation(SwordTraceTipName);
	PreviousSwordTraceMid = IsValidSwordTracePoint(SwordTraceMidName)
		? GetMesh()->GetSocketLocation(SwordTraceMidName)
		: FMath::Lerp(PreviousSwordTraceBase, PreviousSwordTraceTip, 0.5f);
	DamagedActors.Reset();
	bMeleeHitWindowActive = true;
}

void ABHHeroCharacter::UpdateMeleeHitWindow()
{
	if (!HasAuthority() || !bMeleeHitWindowActive || !GetMesh() || !GetWorld() || !BasicAttackDamageEffect)
	{
		return;
	}

	const FVector CurrentSwordTraceBase = GetMesh()->GetSocketLocation(SwordTraceBaseName);
	const FVector CurrentSwordTraceTip = GetMesh()->GetSocketLocation(SwordTraceTipName);
	const FVector CurrentSwordTraceMid = IsValidSwordTracePoint(SwordTraceMidName)
		? GetMesh()->GetSocketLocation(SwordTraceMidName)
		: FMath::Lerp(CurrentSwordTraceBase, CurrentSwordTraceTip, 0.5f);
	TArray<FHitResult> HitResults;
	bool bBlockedByWorld = false;
	TraceSwordPoint(PreviousSwordTraceBase, CurrentSwordTraceBase, HitResults, bBlockedByWorld);
	if (!bBlockedByWorld)
	{
		TraceSwordPoint(PreviousSwordTraceMid, CurrentSwordTraceMid, HitResults, bBlockedByWorld);
	}
	if (!bBlockedByWorld)
	{
		TraceSwordPoint(PreviousSwordTraceTip, CurrentSwordTraceTip, HitResults, bBlockedByWorld);
	}

	ApplyBasicAttackDamage(HitResults);
	PreviousSwordTraceBase = CurrentSwordTraceBase;
	PreviousSwordTraceMid = CurrentSwordTraceMid;
	PreviousSwordTraceTip = CurrentSwordTraceTip;
}

void ABHHeroCharacter::EndMeleeHitWindow()
{
	bMeleeHitWindowActive = false;
	DamagedActors.Reset();
}

void ABHHeroCharacter::ResolveComboBranch()
{
	if (!HasAuthority() || !bComboActive)
	{
		return;
	}

	if (bComboInputBuffered && CurrentComboAttackIndex + 1 < ComboAttackSectionNames.Num())
	{
		bComboInputBuffered = false;
		++CurrentComboAttackIndex;
		MulticastPlayBasicAttackSection(ComboAttackSectionNames[CurrentComboAttackIndex]);
		return;
	}

	EndBasicAttackCombo();
}

void ABHHeroCharacter::EndBasicAttackCombo()
{
	bComboActive = false;
	bComboInputBuffered = false;
	CurrentComboAttackIndex = INDEX_NONE;
}

bool ABHHeroCharacter::IsValidSwordTracePoint(const FName& PointName) const
{
	return GetMesh() && (GetMesh()->DoesSocketExist(PointName) || GetMesh()->GetBoneIndex(PointName) != INDEX_NONE);
}

void ABHHeroCharacter::TraceSwordPoint(const FVector& TraceStart, const FVector& TraceEnd, TArray<FHitResult>& HitResults, bool& bBlockedByWorld) const
{
	if (!GetWorld() || bBlockedByWorld)
	{
		return;
	}

	FCollisionQueryParams QueryParameters(SCENE_QUERY_STAT(BHSwordTrace), false, this);
	QueryParameters.AddIgnoredActor(this);
	const FCollisionShape TraceShape = FCollisionShape::MakeSphere(SwordTraceRadius);
	FCollisionObjectQueryParams WorldObjectQueryParams;
	WorldObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
	WorldObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FHitResult WorldHit;
	const bool bHitWorld = bStopSwordTraceOnWorld && GetWorld()->SweepSingleByObjectType(WorldHit, TraceStart, TraceEnd, FQuat::Identity, WorldObjectQueryParams, TraceShape, QueryParameters);

	FCollisionObjectQueryParams PawnObjectQueryParams;
	PawnObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);
	TArray<FHitResult> PawnHits;
	GetWorld()->SweepMultiByObjectType(PawnHits, TraceStart, TraceEnd, FQuat::Identity, PawnObjectQueryParams, TraceShape, QueryParameters);

	for (const FHitResult& PawnHit : PawnHits)
	{
		if (!bHitWorld || PawnHit.Distance <= WorldHit.Distance)
		{
			HitResults.Add(PawnHit);
		}
	}

	bBlockedByWorld = bHitWorld;
}

void ABHHeroCharacter::ApplyBasicAttackDamage(const TArray<FHitResult>& HitResults)
{
	UBHAbilitySystemComponent* SourceAbilitySystem = GetBHAbilitySystemComponent();
	if (!SourceAbilitySystem)
	{
		return;
	}

	for (const FHitResult& HitResult : HitResults)
	{
		if (MaxSwordHitActors > 0 && DamagedActors.Num() >= MaxSwordHitActors)
		{
			return;
		}

		AActor* HitActor = HitResult.GetActor();
		if (!HitActor || DamagedActors.Contains(HitActor))
		{
			continue;
		}

		IAbilitySystemInterface* TargetAbilitySystemInterface = Cast<IAbilitySystemInterface>(HitActor);
		UAbilitySystemComponent* TargetAbilitySystem = TargetAbilitySystemInterface ? TargetAbilitySystemInterface->GetAbilitySystemComponent() : nullptr;
		if (!TargetAbilitySystem || TargetAbilitySystem == SourceAbilitySystem)
		{
			continue;
		}

		FGameplayEffectContextHandle EffectContext = SourceAbilitySystem->MakeEffectContext();
		EffectContext.AddSourceObject(this);
		const FGameplayEffectSpecHandle EffectSpec = SourceAbilitySystem->MakeOutgoingSpec(BasicAttackDamageEffect, 1.0f, EffectContext);
		if (EffectSpec.IsValid())
		{
			SourceAbilitySystem->ApplyGameplayEffectSpecToTarget(*EffectSpec.Data.Get(), TargetAbilitySystem);
			DamagedActors.Add(HitActor);
			UE_LOG(LogProjectBH, Display, TEXT("%s sword hit %s."), *GetName(), *HitActor->GetName());
		}
	}
}
