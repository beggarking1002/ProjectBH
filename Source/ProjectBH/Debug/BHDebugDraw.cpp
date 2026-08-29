// Copyright ProjectBH. All Rights Reserved.

#include "BHDebugDraw.h"

#include "../ProjectBH.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarBHDebugEnabled(
		TEXT("bh.Debug.Enabled"),
		1,
		TEXT("Master switch for ProjectBH runtime debug drawing. 0: Off, 1: On."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarBHDebugCrowd(
		TEXT("bh.Debug.Crowd"),
		1,
		TEXT("Enemy state text and movement-path drawing. 0: Off, 1: On."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarBHDebugSlots(
		TEXT("bh.Debug.Slots"),
		1,
		TEXT("Combat slots, rings, anchor, and spacing diagnostics. 0: Off, 1: On."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarBHDebugPool(
		TEXT("bh.Debug.Pool"),
		1,
		TEXT("Enemy pool status drawing. 0: Off, 1: On."),
		ECVF_Default);

	bool IsCategoryEnabled(const TAutoConsoleVariable<int32>& CategoryVariable)
	{
		return CVarBHDebugEnabled.GetValueOnGameThread() != 0
			&& CategoryVariable.GetValueOnGameThread() != 0;
	}

	void ToggleAllDebugDraw()
	{
		const bool bEnable = CVarBHDebugEnabled.GetValueOnGameThread() == 0;
		CVarBHDebugEnabled->Set(bEnable ? 1 : 0, ECVF_SetByConsole);
		const FString StateText = FString::Printf(
			TEXT("ProjectBH debug drawing: %s"),
			bEnable ? TEXT("ON") : TEXT("OFF"));
		UE_LOG(LogProjectBH, Display, TEXT("%s"), *StateText);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				INDEX_NONE,
				2.0f,
				bEnable ? FColor::Green : FColor::Yellow,
				StateText);
		}
	}

	FAutoConsoleCommand ToggleBHDebugCommand(
		TEXT("bh.Debug.Toggle"),
		TEXT("Toggles all ProjectBH runtime debug drawing."),
		FConsoleCommandDelegate::CreateStatic(&ToggleAllDebugDraw));
}

bool BHDebugDraw::IsCrowdEnabled(bool bInstanceEnabled)
{
	return bInstanceEnabled && IsCategoryEnabled(CVarBHDebugCrowd);
}

bool BHDebugDraw::IsSlotsEnabled(bool bInstanceEnabled)
{
	return bInstanceEnabled && IsCategoryEnabled(CVarBHDebugSlots);
}

bool BHDebugDraw::IsPoolEnabled(bool bInstanceEnabled)
{
	return bInstanceEnabled && IsCategoryEnabled(CVarBHDebugPool);
}
