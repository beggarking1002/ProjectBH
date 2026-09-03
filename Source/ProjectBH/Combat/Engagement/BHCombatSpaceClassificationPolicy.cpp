// Copyright ProjectBH. All Rights Reserved.

#include "BHCombatSpaceClassificationPolicy.h"

#include "../../Components/Combat/CombatEngagementSlotComponent.h"

FBHCombatSpaceClassificationResult FBHCombatSpaceClassificationPolicy::Evaluate(
	const FBHCombatSpaceClassificationInput& Input)
{
	FBHCombatSpaceClassificationResult Result;
	Result.CandidateMode = Input.CurrentMode;
	Result.bCorridorEdgePocketActive = Input.bCorridorEdgePocketActive;

	const float EnterWidth = FMath::Max(0.0f, Input.CorridorEnterMaxWidth);
	const float ExitWidth = FMath::Max(EnterWidth, Input.CorridorExitMinWidth);
	const float EnterRatio = FMath::Max(1.0f, Input.CorridorEnterAspectRatio);
	const float ExitRatio = FMath::Clamp(Input.CorridorExitAspectRatio, 1.0f, EnterRatio);
	const bool bCorridorEvidence = Input.CorridorAxisLength >= Input.CorridorMinimumAxisLength
		&& Input.CorridorWidth <= EnterWidth
		&& Input.CorridorAspectRatio >= EnterRatio;
	const bool bOpenEvidence = Input.CorridorAxisLength < Input.CorridorMinimumAxisLength
		|| Input.CorridorWidth >= ExitWidth
		|| Input.CorridorAspectRatio <= ExitRatio;
	const bool bCorridorExitShapeEvidence =
		Input.CorridorAxisLength >= Input.CorridorMinimumAxisLength
		&& Input.CorridorWidth < ExitWidth
		&& Input.CorridorAspectRatio > ExitRatio;

	const bool bWasPocket = Input.CurrentMode == EBHCombatSpaceMode::Pocket;
	const float RequiredPocketBlockedFraction = bWasPocket
		? FMath::Clamp(Input.PocketExitBlockedFraction, 0.0f, 1.0f)
		: FMath::Clamp(Input.PocketEnterBlockedFraction, 0.0f, 1.0f);
	const float RequiredPocketOpenArc = bWasPocket
		? FMath::Clamp(Input.PocketExitMinimumOpenArc, 0.0f, 360.0f)
		: FMath::Clamp(Input.PocketEnterMinimumOpenArc, 0.0f, 360.0f);
	const bool bPocketEvidence = Input.PocketBlockedFraction >= RequiredPocketBlockedFraction
		&& Input.PocketOpenArc >= RequiredPocketOpenArc;

	const bool bUseCorridorEdgeExitThreshold = bWasPocket
		&& Input.bCorridorEdgePocketActive;
	const float RequiredCorridorEdgeDistance = bUseCorridorEdgeExitThreshold
		? FMath::Max(0.0f, Input.PocketCorridorEdgeExitDistance)
		: FMath::Max(0.0f, Input.PocketCorridorEdgeEnterDistance);
	const float RequiredCorridorEdgeClearanceDifference = bUseCorridorEdgeExitThreshold
		? FMath::Max(0.0f, Input.PocketCorridorEdgeExitClearanceDifference)
		: FMath::Max(0.0f, Input.PocketCorridorEdgeEnterClearanceDifference);
	const bool bHasCorridorEdgeContext = Input.CurrentMode == EBHCombatSpaceMode::Corridor
		|| Input.bCorridorEdgePocketActive;
	const bool bCorridorEdgePocketEvidence = bHasCorridorEdgeContext
		&& Input.CorridorEdgeNearDistance <= RequiredCorridorEdgeDistance
		&& Input.CorridorEdgeClearanceDifference >= RequiredCorridorEdgeClearanceDifference;

	if (Input.CurrentMode == EBHCombatSpaceMode::Corridor)
	{
		Result.bCorridorEdgePocketActive = bCorridorEdgePocketEvidence;
	}
	else if (!bWasPocket
		|| (Input.bCorridorEdgePocketActive && !bCorridorEdgePocketEvidence))
	{
		Result.bCorridorEdgePocketActive = false;
	}

	const bool bCenteredCorridorRecovery = bWasPocket
		&& bCorridorExitShapeEvidence
		&& !bCorridorEdgePocketEvidence;
	if (Input.CurrentMode == EBHCombatSpaceMode::Corridor
		&& Input.bCorridorMouthDetected)
	{
		// MouthMixed is a Corridor layout sub-state, not a request to reclassify.
		Result.CandidateMode = EBHCombatSpaceMode::Corridor;
	}
	else if (bCorridorEdgePocketEvidence)
	{
		Result.CandidateMode = EBHCombatSpaceMode::Pocket;
	}
	else if (bCorridorEvidence || bCenteredCorridorRecovery)
	{
		Result.CandidateMode = EBHCombatSpaceMode::Corridor;
	}
	else if (bPocketEvidence)
	{
		Result.CandidateMode = EBHCombatSpaceMode::Pocket;
	}
	else if (bOpenEvidence)
	{
		Result.CandidateMode = EBHCombatSpaceMode::Open;
	}

	Result.RequiredTransitionDuration = FMath::Max(0.0f, Input.CorridorExitDuration);
	if (Result.CandidateMode == EBHCombatSpaceMode::Pocket)
	{
		Result.RequiredTransitionDuration = FMath::Max(0.0f, Input.PocketEnterDuration);
	}
	else if (bWasPocket)
	{
		Result.RequiredTransitionDuration = FMath::Max(0.0f, Input.PocketExitDuration);
	}
	else if (Result.CandidateMode == EBHCombatSpaceMode::Corridor)
	{
		Result.RequiredTransitionDuration = FMath::Max(0.0f, Input.CorridorEnterDuration);
	}
	return Result;
}
