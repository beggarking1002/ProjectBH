// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"

namespace BHCollisionChannels
{
	/** Living Enemy capsule object type. Enemy capsules ignore only this channel. */
	inline constexpr ECollisionChannel EnemyPawn = ECC_GameTraceChannel1;
}
