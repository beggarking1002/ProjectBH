// Copyright ProjectBH. All Rights Reserved.

#include "BHAnimNotify_BasicAttackHit.h"

#include "../BHHeroCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UBHAnimNotify_BasicAttackHit::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (ABHHeroCharacter* HeroCharacter = MeshComp ? Cast<ABHHeroCharacter>(MeshComp->GetOwner()) : nullptr)
	{
		HeroCharacter->PerformBasicAttackHit();
	}
}
