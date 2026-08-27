// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"
#include "BHAnimNotify_EnemyAttackHit.generated.h"

/** Executes the authoritative hit check for the current enemy attack target. */
UCLASS(meta = (DisplayName = "BH Enemy Attack Hit"))
class PROJECTBH_API UBHAnimNotify_EnemyAttackHit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
