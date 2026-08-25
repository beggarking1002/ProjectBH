// Copyright ProjectBH. All Rights Reserved.

#include "BHAnimNotifyState_MeleeHitWindow.h"
#include "ProjectBH/BHHeroCharacter.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	ABHHeroCharacter* GetHeroCharacter(USkeletalMeshComponent* MeshComp)
	{
		return MeshComp ? Cast<ABHHeroCharacter>(MeshComp->GetOwner()) : nullptr;
	}
}

void UBHAnimNotifyState_MeleeHitWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (ABHHeroCharacter* HeroCharacter = GetHeroCharacter(MeshComp))
	{
		HeroCharacter->BeginMeleeHitWindow();
	}
}

void UBHAnimNotifyState_MeleeHitWindow::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	if (ABHHeroCharacter* HeroCharacter = GetHeroCharacter(MeshComp))
	{
		HeroCharacter->UpdateMeleeHitWindow();
	}
}

void UBHAnimNotifyState_MeleeHitWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (ABHHeroCharacter* HeroCharacter = GetHeroCharacter(MeshComp))
	{
		HeroCharacter->EndMeleeHitWindow();
	}
}
