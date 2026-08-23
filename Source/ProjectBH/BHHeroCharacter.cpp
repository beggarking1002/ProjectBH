// Copyright ProjectBH. All Rights Reserved.

#include "BHHeroCharacter.h"

#include "BHGameplayTags.h"
#include "AbilitySystem/BHAbilitySystemComponent.h"
#include "ProjectBH.h"
#include "Weapons/BHWeapon.h"
#include "AbilitySystemInterface.h"
#include "Animation/AnimMontage.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/Input/BHInputComponent.h"
#include "GameplayEffect.h"
#include "Net/UnrealNetwork.h"

namespace BHHeroWeaponSockets
{
	const FName RightHand(TEXT("Weapon_R"));
}

ABHHeroCharacter::ABHHeroCharacter()
{
	AutoPossessAI = EAutoPossessAI::Disabled;
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = 400.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.0f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->TargetArmLength = 200.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 55.0f, 65.0f);
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void ABHHeroCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	SpawnStartingWeapon();
}

void ABHHeroCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABHHeroCharacter, EquippedWeapon);
}

void ABHHeroCharacter::SpawnStartingWeapon()
{
	if (!HasAuthority() || !StartingWeaponClass || EquippedWeapon)
	{
		return;
	}

	if (!GetMesh()->DoesSocketExist(BHHeroWeaponSockets::RightHand))
	{
		UE_LOG(LogProjectBH, Error, TEXT("%s requires a '%s' socket on its skeletal mesh before equipping a weapon."), *GetName(), *BHHeroWeaponSockets::RightHand.ToString());
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	EquippedWeapon = GetWorld()->SpawnActor<ABHWeapon>(StartingWeaponClass, FTransform::Identity, SpawnParameters);
	if (!EquippedWeapon)
	{
		UE_LOG(LogProjectBH, Error, TEXT("%s failed to spawn its starting weapon."), *GetName());
		return;
	}

	EquippedWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, BHHeroWeaponSockets::RightHand);
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
	if (!BasicAttackMontage || !GetWorld() || GetWorld()->GetTimeSeconds() < NextBasicAttackTime)
	{
		return;
	}

	NextBasicAttackTime = GetWorld()->GetTimeSeconds() + BasicAttackCooldown;
	MulticastPlayBasicAttack();
}

void ABHHeroCharacter::MulticastPlayBasicAttack_Implementation()
{
	if (BasicAttackMontage)
	{
		PlayAnimMontage(BasicAttackMontage);
	}
}

void ABHHeroCharacter::PerformBasicAttackHit()
{
	if (!HasAuthority() || !BasicAttackDamageEffect)
	{
		return;
	}

	UBHAbilitySystemComponent* SourceAbilitySystem = GetBHAbilitySystemComponent();
	if (!SourceAbilitySystem || !GetWorld())
	{
		return;
	}

	const FVector TraceStart = GetActorLocation() + FVector(0.0f, 0.0f, 50.0f);
	const FVector TraceEnd = TraceStart + GetActorForwardVector() * BasicAttackRange;

	FCollisionQueryParams QueryParameters(SCENE_QUERY_STAT(BHBasicAttack), false, this);
	QueryParameters.AddIgnoredActor(this);
	if (EquippedWeapon)
	{
		QueryParameters.AddIgnoredActor(EquippedWeapon);
	}

	TArray<FHitResult> HitResults;
	const FCollisionShape TraceShape = FCollisionShape::MakeSphere(BasicAttackRadius);
	GetWorld()->SweepMultiByChannel(HitResults, TraceStart, TraceEnd, FQuat::Identity, ECC_Pawn, TraceShape, QueryParameters);

	TSet<AActor*> DamagedActors;
	for (const FHitResult& HitResult : HitResults)
	{
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
		}
	}
}
