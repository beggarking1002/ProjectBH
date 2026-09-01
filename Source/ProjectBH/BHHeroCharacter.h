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
class UCombatEngagementSlotComponent;
struct FInputActionValue;

/** Player-controlled character foundation for the first combat slice. */
UCLASS()
class PROJECTBH_API ABHHeroCharacter : public ABHBaseCharacter
{
	GENERATED_BODY()

public:
	ABHHeroCharacter();

	UCameraComponent* GetFollowCamera() const { return FollowCamera; }
	UCombatEngagementSlotComponent* GetCombatEngagementSlotComponent() const { return CombatEngagementSlots; }

	/** Legacy one-frame attack notify support. New montages should use ANS Melee Hit Window. */
	void PerformBasicAttackHit();

	/** Called by ANS Melee Hit Window. Damage traces execute on the server only. */
	void BeginMeleeHitWindow();
	void UpdateMeleeHitWindow();
	void EndMeleeHitWindow();

	/** Called at the end of every Recovery section to continue or finish the server-authoritative combo. */
	void ResolveComboBranch();

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	
	/** Positions the camera behind the character and handles collision with level geometry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Player view camera attached to the end of CameraBoom. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	/** Owns the server-authoritative Attack and Wait positions around this hero. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Engagement Slots", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCombatEngagementSlotComponent> CombatEngagementSlots;
	
#pragma region Inputs
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CharacterData", meta = (AllowPrivateAccess = "true"))
	UDataAsset_InputConfig* InputConfigDataAsset;

	void Input_Move(const FInputActionValue& InputActionValue);
	void Input_Look(const FInputActionValue& InputActionValue);
	void Input_BasicAttack();
	
#pragma endregion 

	/** Montage containing Attack_A/B/C and Recovery_A/B/C sections. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> BasicAttackMontage;

	/** Ordered Attack sections. Recovery sections branch to the next entry through AN Combo Branch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (AllowPrivateAccess = "true"))
	TArray<FName> ComboAttackSectionNames;

	/** Gameplay Effect applied to valid targets inside the attack hit sweep. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UGameplayEffect> BasicAttackDamageEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (AllowPrivateAccess = "true"))
	FName SwordTraceBaseName = TEXT("FX_Sword_Bottom");

	/** Optional. When unset, the midpoint is interpolated from the base and tip trace points. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (AllowPrivateAccess = "true"))
	FName SwordTraceMidName;

	/** Create these sockets on the ProjectBH-derived Greystone skeletal mesh. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (AllowPrivateAccess = "true"))
	FName SwordTraceTipName = TEXT("FX_Sword_Top");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float SwordTraceRadius = 12.0f;

	/** Maximum distinct enemies damaged during one hit window. Zero means unlimited. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (ClampMin = "0", UIMin = "0", AllowPrivateAccess = "true"))
	int32 MaxSwordHitActors = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Basic Attack", meta = (AllowPrivateAccess = "true"))
	bool bStopSwordTraceOnWorld = true;

	UFUNCTION(Server, Reliable)
	void ServerRequestBasicAttack();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayBasicAttackSection(FName SectionName);

	void TryStartBasicAttack();
	void EndBasicAttackCombo();
	bool IsValidSwordTracePoint(const FName& PointName) const;
	void TraceSwordPoint(const FVector& TraceStart, const FVector& TraceEnd, TArray<FHitResult>& HitResults, bool& bBlockedByWorld) const;
	void ApplyBasicAttackDamage(const TArray<FHitResult>& HitResults);

	FVector PreviousSwordTraceBase = FVector::ZeroVector;
	FVector PreviousSwordTraceMid = FVector::ZeroVector;
	FVector PreviousSwordTraceTip = FVector::ZeroVector;
	TSet<AActor*> DamagedActors;
	int32 CurrentComboAttackIndex = INDEX_NONE;
	bool bComboInputBuffered = false;
	bool bComboActive = false;
	bool bMeleeHitWindowActive = false;
};
