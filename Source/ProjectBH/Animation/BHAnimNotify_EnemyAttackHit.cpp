// Copyright ProjectBH. All Rights Reserved.

#include "BHAnimNotify_EnemyAttackHit.h"

#include "../Enemies/BHEnemy.h"
#include "Components/SkeletalMeshComponent.h"

void UBHAnimNotify_EnemyAttackHit::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (ABHEnemy* Enemy = MeshComp ? Cast<ABHEnemy>(MeshComp->GetOwner()) : nullptr)
	{
		Enemy->PerformBasicAttackHit();
	}
}
