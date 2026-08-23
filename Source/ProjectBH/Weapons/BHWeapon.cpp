// Copyright ProjectBH. All Rights Reserved.

#include "BHWeapon.h"

#include "Components/StaticMeshComponent.h"

ABHWeapon::ABHWeapon()
{
	bReplicates = true;
	bNetUseOwnerRelevancy = true;
	SetReplicateMovement(false);

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetGenerateOverlapEvents(false);
}
