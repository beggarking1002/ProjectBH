// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BHBaseCharacter.h"
#include "BHHeroCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;

/** Player-controlled character foundation for the first combat slice. */
UCLASS()
class PROJECTBH_API ABHHeroCharacter : public ABHBaseCharacter
{
	GENERATED_BODY()

public:
	ABHHeroCharacter();

	UCameraComponent* GetFollowCamera() const { return FollowCamera; }

protected:
	/** Positions the camera behind the character and handles collision with level geometry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Player view camera attached to the end of CameraBoom. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;
};
