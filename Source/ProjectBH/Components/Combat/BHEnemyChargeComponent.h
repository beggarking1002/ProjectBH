// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BHEnemyChargeComponent.generated.h"

class AActor;
class ABHEnemy;
class UAnimMontage;
struct FBHEnemyChargeConfig;

/**
 * Owns fixed-distance Root Motion charge execution for an enemy.
 *
 * The AI controller chooses when to try. This component validates a straight
 * lane, suspends path following, and sweeps aside enemies while the Montage
 * remains the sole source of forward displacement.
 */
UCLASS(ClassGroup = (Combat))
class PROJECTBH_API UBHEnemyChargeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBHEnemyChargeComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	bool TryStartCharge(AActor* TargetActor);
	bool IsChargeActive() const { return bChargeActive; }
	FVector GetChargeDirection() const { return ChargeDirection; }

	/** Called by the owner before normal recovery begins. */
	void NotifyOwningAttackEnded(UAnimMontage* EndedMontage);

	/** Interrupts the current charge and optionally starts its cooldown. */
	void CancelCharge(bool bStartCooldown = true);

	/** Clears runtime state when an object-pooled enemy begins a new life. */
	void ResetChargeState();

private:
	const FBHEnemyChargeConfig* GetChargeConfig() const;
	bool ResolveClearChargeLane(
		AActor* TargetActor,
		const FBHEnemyChargeConfig& Config,
		float AuthoredTravelDistance,
		FVector& OutDirection,
		FVector& OutEndLocation) const;
	bool GetAuthoredTravelDistance(
		UAnimMontage* Montage,
		const FBHEnemyChargeConfig& Config,
		float& OutDistance,
		float& OutTravelYawOffset,
		float& OutMaximumLateralDrift,
		float& OutAuthoredYaw) const;
	void SweepAsideEnemies(
		const FVector& SweepStart,
		const FVector& SweepEnd,
		const FBHEnemyChargeConfig& Config);
	void FinishCharge(bool bStartCooldown);

	UPROPERTY(Transient)
	TObjectPtr<ABHEnemy> OwningEnemy;

	TWeakObjectPtr<UAnimMontage> ChargeMontage;
	TSet<TWeakObjectPtr<ABHEnemy>> KnockedAsideEnemies;
	FVector ChargeDirection = FVector::ZeroVector;
	FVector PreviousChargeLocation = FVector::ZeroVector;
	float NextChargeAllowedTime = 0.0f;
	TEnumAsByte<ECollisionResponse> PreviousPawnCollisionResponse = ECR_Block;
	bool bPreviousOrientRotationToMovement = false;
	bool bPreviousUseControllerDesiredRotation = true;
	bool bChargeActive = false;
	bool bLoggedInvalidRootMotion = false;
};
