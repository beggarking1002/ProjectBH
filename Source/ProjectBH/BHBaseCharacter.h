// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BHBaseCharacter.generated.h"

/** Common network-ready character base for heroes and future AI characters. */
UCLASS(Abstract)
class PROJECTBH_API ABHBaseCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ABHBaseCharacter();
};
