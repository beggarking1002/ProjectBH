// Copyright ProjectBH. All Rights Reserved.

#pragma once

namespace BHDebugDraw
{
	/** Runtime master switch combined with the per-instance Crowd debug flag. */
	PROJECTBH_API bool IsCrowdEnabled(bool bInstanceEnabled);

	/** Runtime master switch combined with the per-instance Slot debug flag. */
	PROJECTBH_API bool IsSlotsEnabled(bool bInstanceEnabled);

	/** Runtime master switch combined with the per-instance Pool debug flag. */
	PROJECTBH_API bool IsPoolEnabled(bool bInstanceEnabled);
}
