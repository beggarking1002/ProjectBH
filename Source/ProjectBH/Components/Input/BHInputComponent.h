#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputComponent.h"
#include "ProjectBH/DataAssets/Input/DataAsset_InputConfig.h"
#include "BHInputComponent.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTBH_API UBHInputComponent : public UEnhancedInputComponent 
{
	GENERATED_BODY()
	
public:
	template<class UserObject,typename CallbackFunc>
	void BindNativeInputAction(const UDataAsset_InputConfig* InInputConfig,const FGameplayTag& InInputTag,ETriggerEvent TriggerEvent,UserObject* ContextObject,CallbackFunc Func);
};

template<class UserObject, typename CallbackFunc>
inline void UBHInputComponent::BindNativeInputAction(const UDataAsset_InputConfig* InInputConfig, const FGameplayTag& InInputTag, ETriggerEvent TriggerEvent, UserObject* ContextObject, CallbackFunc Func)
{
	checkf(InInputConfig,TEXT("Input config data asset is null,can not proceed with binding"));

	if (UInputAction* FoundAction = InInputConfig->FindNativeInputActionByTag(InInputTag))
	{
		BindAction(FoundAction,TriggerEvent,ContextObject,Func);
	}
}