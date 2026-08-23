// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BHBaseCharacter.h"
#include "BHHeroCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UDataAsset_InputConfig;
class UGameplayEffect;
class UAnimMontage;
class ABHWeapon;
struct FInputActionValue;

/** Player-controlled character foundation for the first combat slice. */
UCLASS()
class PROJECTBH_API ABHHeroCharacter : public ABHBaseCharacter
{
	GENERATED_BODY()

public:
	ABHHeroCharacter();

	UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	/** Called by the attack-hit Anim Notify. Damage traces execute on the server only. */
	void PerformBasicAttackHit();

protected:
	//~ Begin APawn Interface.
	virtual void PossessedBy(AController* NewController) override;
	//~ End APawn Interface

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	
	/** Positions the camera behind the character and handles collision with level geometry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Player view camera attached to the end of CameraBoom. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;
	
#pragma region Inputs
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CharacterData", meta = (AllowPrivateAccess = "true"))
	UDataAsset_InputConfig* InputConfigDataAsset;

	/** Weapon class equipped by this hero when the server possesses it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<ABHWeapon> StartingWeaponClass;

	/** Current server-authoritative equipped weapon. */
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Equipment", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ABHWeapon> EquippedWeapon;

	void SpawnStartingWeapon();

	void Input_Move(const FInputActionValue& InputActionValue);
	void Input_Look(const FInputActionValue& InputActionValue);
	void Input_BasicAttack();
	
#pragma endregion 

	/** Montage played for the first axe attack. Configure it on BP_Hero. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> BasicAttackMontage;

	/** Gameplay Effect applied to valid targets inside the attack hit sweep. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UGameplayEffect> BasicAttackDamageEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float BasicAttackCooldown = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float BasicAttackRange = 175.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float BasicAttackRadius = 75.0f;

	UFUNCTION(Server, Reliable)
	void ServerRequestBasicAttack();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayBasicAttack();

	void TryStartBasicAttack();

	float NextBasicAttackTime = 0.0f;
};
