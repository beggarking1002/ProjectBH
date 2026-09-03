// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EBHCombatSpaceMode : uint8;

/** Inputs required to classify already measured combat-space geometry. */
struct FBHCombatSpaceClassificationInput
{
	EBHCombatSpaceMode CurrentMode{};
	bool bCorridorMouthDetected = false;
	bool bCorridorEdgePocketActive = false;

	float CorridorAxisLength = 0.0f;
	float CorridorWidth = 0.0f;
	float CorridorAspectRatio = 0.0f;
	float CorridorEdgeNearDistance = 0.0f;
	float CorridorEdgeClearanceDifference = 0.0f;
	float PocketBlockedFraction = 0.0f;
	float PocketOpenArc = 0.0f;

	float CorridorMinimumAxisLength = 0.0f;
	float CorridorEnterMaxWidth = 0.0f;
	float CorridorExitMinWidth = 0.0f;
	float CorridorEnterAspectRatio = 1.0f;
	float CorridorExitAspectRatio = 1.0f;
	float CorridorEnterDuration = 0.0f;
	float CorridorExitDuration = 0.0f;
	float PocketEnterBlockedFraction = 0.0f;
	float PocketExitBlockedFraction = 0.0f;
	float PocketEnterMinimumOpenArc = 0.0f;
	float PocketExitMinimumOpenArc = 0.0f;
	float PocketCorridorEdgeEnterDistance = 0.0f;
	float PocketCorridorEdgeExitDistance = 0.0f;
	float PocketCorridorEdgeEnterClearanceDifference = 0.0f;
	float PocketCorridorEdgeExitClearanceDifference = 0.0f;
	float PocketEnterDuration = 0.0f;
	float PocketExitDuration = 0.0f;
};

struct FBHCombatSpaceClassificationResult
{
	EBHCombatSpaceMode CandidateMode{};
	bool bCorridorEdgePocketActive = false;
	float RequiredTransitionDuration = 0.0f;
};

/** Pure hysteresis policy for OPEN / POCKET / CORRIDOR classification. */
class PROJECTBH_API FBHCombatSpaceClassificationPolicy final
{
public:
	static FBHCombatSpaceClassificationResult Evaluate(
		const FBHCombatSpaceClassificationInput& Input);
};
