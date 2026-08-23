// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BHWeapon.generated.h"

class UStaticMeshComponent;

/** Replicated world actor used for an equipped weapon. */
UCLASS()
class PROJECTBH_API ABHWeapon : public AActor
{
	GENERATED_BODY()

public:
	ABHWeapon();

	UStaticMeshComponent* GetWeaponMesh() const { return WeaponMesh; }

protected:
	/** Visual mesh. Weapon blueprints assign the concrete weapon asset here. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;
};
