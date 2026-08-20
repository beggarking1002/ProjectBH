// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BHBaseCharacter.h"
#include "BHHeroCharacter.generated.h"

/** Player-controlled character foundation for the first combat slice. */
UCLASS()
class PROJECTBH_API ABHHeroCharacter : public ABHBaseCharacter
{
	GENERATED_BODY()

public:
	ABHHeroCharacter();
};
