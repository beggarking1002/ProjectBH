// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"
#include "BHAnimNotify_BasicAttackHit.generated.h"

/** Opens the authoritative hit frame for the hero's first basic attack. */
UCLASS(meta = (DisplayName = "BH Basic Attack Hit"))
class PROJECTBH_API UBHAnimNotify_BasicAttackHit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
