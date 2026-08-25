// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"
#include "BHAnimNotify_ComboBranch.generated.h"

/** Resolves the buffered combo input at the end of a Recovery montage section. */
UCLASS(meta = (DisplayName = "AN Combo Branch"))
class PROJECTBH_API UBHAnimNotify_ComboBranch : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
