// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "BHAnimNotifyState_MeleeHitWindow.generated.h"

/** Enables authoritative multi-sphere weapon tracing for the active portion of a melee animation. */
UCLASS(meta = (DisplayName = "ANS Melee Hit Window"))
class PROJECTBH_API UBHAnimNotifyState_MeleeHitWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
