// Copyright ProjectBH. All Rights Reserved.

#include "CombatEngagementSlotComponent.h"

#include "../../AI/BHCrowdEnemyAIController.h"
#include "../../Debug/BHDebugDraw.h"
#include "../../BHHeroCharacter.h"
#include "../../Enemies/BHEnemy.h"
#include "../../ProjectBH.h"
#include "AI/NavigationSystemBase.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

UCombatEngagementSlotComponent::UCombatEngagementSlotComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UCombatEngagementSlotComponent::BeginPlay()
{
	Super::BeginPlay();

	InitializeSlots();
	EngagementAnchorLocation = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	LastReformOwnerLocation = EngagementAnchorLocation;
	LastReformAnchorLocation = EngagementAnchorLocation;
}

void UCombatEngagementSlotComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AttackReservations.Reset();
	AttackSlotVacantElapsed.Reset();
	AttackHandoverCandidates.Reset();
	AttackHandoverElapsed.Reset();
	AttackHandoverBlockedUntil.Reset();
	WaitReservations.Reset();
	HoldingReservations.Reset();
	ResetCoreIntrusionHandoverTracking();
	EngagementQueue.Reset();
	SpaceProbeEndpoints.Reset();
	SpaceProbeBlocked.Reset();

	Super::EndPlay(EndPlayReason);
}

void UCombatEngagementSlotComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	PruneInvalidReservations();
	UpdateCombatSpaceAnalysis(DeltaTime);
	RefreshCorridorFormationCapacity(DeltaTime);
	RefreshPocketFormationCapacity();
	UpdateEngagementAnchor(DeltaTime);
	RefreshCorridorRowLayout(DeltaTime);
	RefreshInitialFormationPhase();
	UpdateAttackVacancyTimers(DeltaTime);
	if (!IsInitialFormationActive())
	{
		RefreshCoreIntrusionHandover(DeltaTime);
		RefreshPromotions();
		RefreshFastAttackHandovers(DeltaTime);
	}
	TryReformFormation();
	if (!BHDebugDraw::IsSlotsEnabled(bDrawDebugSlots))
	{
		return;
	}

	UpdateDebugMetrics();
	DrawDebugSlots();
}

void UCombatEngagementSlotComponent::UpdateCombatSpaceAnalysis(float DeltaTime)
{
	if (!bEnableCombatSpaceAnalysis)
	{
		bHasValidSpaceAnalysis = false;
		SpaceAnalysisElapsed = 0.0f;
		SpaceModeTransitionElapsed = 0.0f;
		return;
	}

	SpaceAnalysisElapsed += DeltaTime;
	const float EffectiveInterval = FMath::Max(0.05f, SpaceAnalysisInterval);
	if (SpaceAnalysisElapsed < EffectiveInterval)
	{
		return;
	}

	const float SampleDeltaTime = SpaceAnalysisElapsed;
	SpaceAnalysisElapsed = 0.0f;
	AnalyzeCombatSpace(SampleDeltaTime);
}

bool UCombatEngagementSlotComponent::MeasureCorridorCrossSection(
	const FVector& DesiredCenter,
	const FVector& AxisDirection,
	float ProbeHalfWidth,
	float& OutWidth,
	FVector& OutCenter,
	FVector& OutSide0,
	FVector& OutSide1) const
{
	OutWidth = 0.0f;
	OutCenter = DesiredCenter;
	OutSide0 = DesiredCenter;
	OutSide1 = DesiredCenter;

	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	const FVector SafeAxis = AxisDirection.GetSafeNormal2D();
	FVector ProjectedOwner;
	if (!Owner
		|| !NavigationSystem
		|| SafeAxis.IsNearlyZero()
		|| !ProjectToNavigation(Owner->GetActorLocation(), ProjectedOwner)
		|| !ProjectToNavigation(DesiredCenter, OutCenter))
	{
		return false;
	}

	FVector CenterPathHit;
	if (NavigationSystem->NavigationRaycast(
		World,
		ProjectedOwner,
		OutCenter,
		CenterPathHit))
	{
		return false;
	}

	const FVector CrossDirection(-SafeAxis.Y, SafeAxis.X, 0.0f);
	const float SafeHalfWidth = FMath::Max(100.0f, ProbeHalfWidth);
	const FVector DesiredSide0 = OutCenter + CrossDirection * SafeHalfWidth;
	const FVector DesiredSide1 = OutCenter - CrossDirection * SafeHalfWidth;
	FVector SideHit;
	const bool bSide0Blocked = NavigationSystem->NavigationRaycast(
		World,
		OutCenter,
		DesiredSide0,
		SideHit);
	OutSide0 = bSide0Blocked ? SideHit : DesiredSide0;
	const bool bSide1Blocked = NavigationSystem->NavigationRaycast(
		World,
		OutCenter,
		DesiredSide1,
		SideHit);
	OutSide1 = bSide1Blocked ? SideHit : DesiredSide1;
	OutWidth = FVector::Dist2D(OutCenter, OutSide0)
		+ FVector::Dist2D(OutCenter, OutSide1);
	return true;
}

void UCombatEngagementSlotComponent::AnalyzeCombatSpace(float SampleDeltaTime)
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	FVector ProjectedOrigin;
	if (!Owner || !NavigationSystem || !ProjectToNavigation(Owner->GetActorLocation(), ProjectedOrigin))
	{
		if (bHasValidSpaceAnalysis && SpaceAnalysisInvalidGraceRemaining > 0.0f)
		{
			// A missing projection sample is not enough evidence to dismantle a
			// committed formation. Keep the last valid geometry for a short grace
			// window while exposing that the current mouth probes are no longer raw.
			SpaceAnalysisInvalidGraceRemaining = FMath::Max(
				0.0f,
				SpaceAnalysisInvalidGraceRemaining - FMath::Max(0.0f, SampleDeltaTime));
			CandidateSpaceMode = CurrentSpaceMode;
			SpaceModeTransitionElapsed = 0.0f;
			bRawCorridorMouthDetected = false;
			bRawCorridorForwardMouthDetected = false;
			bRawCorridorRearMouthDetected = false;
			PendingMouthMixedKind = EMouthMixedKind::None;
			MouthMixedEvidenceElapsed = 0.0f;
			return;
		}
		bHasValidSpaceAnalysis = false;
		SpaceAnalysisInvalidGraceRemaining = 0.0f;
		CandidateSpaceMode = CurrentSpaceMode;
		SpaceModeTransitionElapsed = 0.0f;
		SpaceProbeEndpoints.Reset();
		SpaceProbeBlocked.Reset();
		CorridorAxisProbeIndex = INDEX_NONE;
		CorridorWidthProbeIndex = INDEX_NONE;
		EstimatedCorridorEdgeNearDistance = 0.0f;
		EstimatedCorridorEdgeClearanceDifference = 0.0f;
		bRawCorridorMouthDetected = false;
		bCorridorMouthDetected = false;
		CorridorMouthEvidenceHoldRemaining = 0.0f;
		bRawCorridorForwardMouthDetected = false;
		bRawCorridorRearMouthDetected = false;
		bCorridorForwardMouthDetected = false;
		bCorridorRearMouthDetected = false;
		CorridorForwardMouthEvidenceHoldRemaining = 0.0f;
		CorridorRearMouthEvidenceHoldRemaining = 0.0f;
		bMouthMixedActive = false;
		MouthMixedEvidenceElapsed = 0.0f;
		PendingMouthMixedKind = EMouthMixedKind::None;
		ActiveMouthMixedKind = EMouthMixedKind::None;
		bCorridorForwardCrossSectionValid = false;
		bCorridorRearCrossSectionValid = false;
		EstimatedCorridorForwardWidth = 0.0f;
		EstimatedCorridorRearWidth = 0.0f;
		EstimatedPocketOpenArc = 0.0f;
		EstimatedPocketBlockedFraction = 0.0f;
		return;
	}

	const int32 RequestedProbeCount = FMath::Clamp(SpaceProbeCount, 8, 64);
	const int32 EffectiveProbeCount = FMath::Min(64, ((RequestedProbeCount + 3) / 4) * 4);
	const int32 HalfProbeCount = EffectiveProbeCount / 2;
	const int32 QuarterProbeCount = EffectiveProbeCount / 4;
	const float EffectiveProbeDistance = FMath::Max(100.0f, SpaceProbeDistance);

	SpaceProbeOrigin = ProjectedOrigin;
	SpaceProbeEndpoints.SetNum(EffectiveProbeCount);
	SpaceProbeBlocked.SetNumZeroed(EffectiveProbeCount);
	TArray<float, TInlineAllocator<64>> ProbeDistances;
	ProbeDistances.SetNumZeroed(EffectiveProbeCount);

	for (int32 ProbeIndex = 0; ProbeIndex < EffectiveProbeCount; ++ProbeIndex)
	{
		const float AngleRadians = 2.0f * UE_PI
			* static_cast<float>(ProbeIndex) / static_cast<float>(EffectiveProbeCount);
		const FVector ProbeDirection(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0f);
		const FVector ProbeEnd = ProjectedOrigin + ProbeDirection * EffectiveProbeDistance;
		FVector HitLocation = ProbeEnd;
		const bool bBlocked = NavigationSystem->NavigationRaycast(
			GetWorld(),
			ProjectedOrigin,
			ProbeEnd,
			HitLocation);
		const FVector ReachLocation = bBlocked ? HitLocation : ProbeEnd;

		SpaceProbeEndpoints[ProbeIndex] = ReachLocation;
		SpaceProbeBlocked[ProbeIndex] = bBlocked ? 1 : 0;
		ProbeDistances[ProbeIndex] = FMath::Min(
			EffectiveProbeDistance,
			FVector::Dist2D(ProjectedOrigin, ReachLocation));
	}

	CorridorAxisProbeIndex = 0;
	EstimatedCorridorAxisLength = -1.0f;
	for (int32 PairIndex = 0; PairIndex < HalfProbeCount; ++PairIndex)
	{
		const float PairLength = ProbeDistances[PairIndex]
			+ ProbeDistances[PairIndex + HalfProbeCount];
		if (PairLength > EstimatedCorridorAxisLength)
		{
			EstimatedCorridorAxisLength = PairLength;
			CorridorAxisProbeIndex = PairIndex;
		}
	}

	CorridorWidthProbeIndex = (CorridorAxisProbeIndex + QuarterProbeCount) % HalfProbeCount;
	EstimatedCorridorWidth = ProbeDistances[CorridorWidthProbeIndex]
		+ ProbeDistances[CorridorWidthProbeIndex + HalfProbeCount];
	EstimatedCorridorAspectRatio = EstimatedCorridorAxisLength
		/ FMath::Max(1.0f, EstimatedCorridorWidth);

	// Find the most asymmetric opposite probe pair. A short side plus a much
	// clearer opposite side describes a wall/end edge, while a centered narrow
	// corridor has similarly short probes on both sides and must stay Corridor.
	EstimatedCorridorEdgeNearDistance = EffectiveProbeDistance;
	EstimatedCorridorEdgeClearanceDifference = 0.0f;
	for (int32 PairIndex = 0; PairIndex < HalfProbeCount; ++PairIndex)
	{
		const float Side0Distance = ProbeDistances[PairIndex];
		const float Side1Distance = ProbeDistances[PairIndex + HalfProbeCount];
		const float NearDistance = FMath::Min(Side0Distance, Side1Distance);
		const float ClearanceDifference = FMath::Abs(Side0Distance - Side1Distance);
		if (ClearanceDifference > EstimatedCorridorEdgeClearanceDifference
			|| (FMath::IsNearlyEqual(
				ClearanceDifference,
				EstimatedCorridorEdgeClearanceDifference)
				&& NearDistance < EstimatedCorridorEdgeNearDistance))
		{
			EstimatedCorridorEdgeNearDistance = NearDistance;
			EstimatedCorridorEdgeClearanceDifference = ClearanceDifference;
		}
	}

	const float AxisAngleRadians = 2.0f * UE_PI
		* static_cast<float>(CorridorAxisProbeIndex) / static_cast<float>(EffectiveProbeCount);
	FVector NewCorridorAxis(FMath::Cos(AxisAngleRadians), FMath::Sin(AxisAngleRadians), 0.0f);
	if (FVector::DotProduct(NewCorridorAxis, EstimatedCorridorAxis) < 0.0f)
	{
		NewCorridorAxis *= -1.0f;
	}
	EstimatedCorridorAxis = NewCorridorAxis;

	// A single radial sample can choose a diagonal axis at a corridor mouth.
	// While already in Corridor, use the committed formation axis to inspect
	// forward/rear cross-sections so the opening cannot rotate its own detector.
	FVector MouthSampleAxis = CurrentSpaceMode == EBHCombatSpaceMode::Corridor
		? CorridorFormationRearDirection.GetSafeNormal2D()
		: EstimatedCorridorAxis.GetSafeNormal2D();
	if (MouthSampleAxis.IsNearlyZero())
	{
		MouthSampleAxis = EstimatedCorridorAxis.GetSafeNormal2D();
	}
	const float MouthSampleDistance = FMath::Max(50.0f, CorridorMouthSampleDistance);
	const float MouthProbeHalfWidth = FMath::Max(100.0f, CorridorMouthProbeHalfWidth);
	bCorridorForwardCrossSectionValid = MeasureCorridorCrossSection(
		ProjectedOrigin + MouthSampleAxis * MouthSampleDistance,
		MouthSampleAxis,
		MouthProbeHalfWidth,
		EstimatedCorridorForwardWidth,
		CorridorForwardCrossSectionCenter,
		CorridorForwardCrossSectionSide0,
		CorridorForwardCrossSectionSide1);
	bCorridorRearCrossSectionValid = MeasureCorridorCrossSection(
		ProjectedOrigin - MouthSampleAxis * MouthSampleDistance,
		MouthSampleAxis,
		MouthProbeHalfWidth,
		EstimatedCorridorRearWidth,
		CorridorRearCrossSectionCenter,
		CorridorRearCrossSectionSide0,
		CorridorRearCrossSectionSide1);
	const float NarrowWidthReference = FMath::Min(
		EstimatedCorridorWidth,
		FMath::Max(1.0f, CorridorExitMinWidth));
	const float RequiredMouthWidth = FMath::Max(
		FMath::Max(CorridorExitMinWidth, CorridorMouthMinimumOpenWidth),
		NarrowWidthReference * FMath::Max(1.0f, CorridorMouthExpansionRatio));
	const bool bForwardWide = bCorridorForwardCrossSectionValid
		&& EstimatedCorridorForwardWidth >= RequiredMouthWidth;
	const bool bRearWide = bCorridorRearCrossSectionValid
		&& EstimatedCorridorRearWidth >= RequiredMouthWidth;
	const bool bHasNarrowSample = EstimatedCorridorWidth
		< FMath::Max(0.0f, CorridorExitMinWidth)
		|| (bCorridorForwardCrossSectionValid && !bForwardWide)
		|| (bCorridorRearCrossSectionValid && !bRearWide);
	bRawCorridorForwardMouthDetected = bHasNarrowSample && bForwardWide;
	bRawCorridorRearMouthDetected = bHasNarrowSample && bRearWide;
	bRawCorridorMouthDetected = bRawCorridorForwardMouthDetected
		|| bRawCorridorRearMouthDetected;
	if (CurrentSpaceMode == EBHCombatSpaceMode::Corridor)
	{
		auto UpdateDirectionalMouthEvidence = [this, SampleDeltaTime](
			bool bRawDetected,
			float& HoldRemaining,
			bool& bHeldDetected)
		{
			if (bRawDetected)
			{
				HoldRemaining = FMath::Max(
					FMath::Max(0.0f, CorridorMouthEvidenceHoldDuration),
					FMath::Max(0.0f, MouthMixedEnterDuration));
			}
			else
			{
				HoldRemaining = FMath::Max(
					0.0f,
					HoldRemaining - FMath::Max(0.0f, SampleDeltaTime));
			}
			bHeldDetected = bRawDetected || HoldRemaining > 0.0f;
		};
		UpdateDirectionalMouthEvidence(
			bRawCorridorForwardMouthDetected,
			CorridorForwardMouthEvidenceHoldRemaining,
			bCorridorForwardMouthDetected);
		UpdateDirectionalMouthEvidence(
			bRawCorridorRearMouthDetected,
			CorridorRearMouthEvidenceHoldRemaining,
			bCorridorRearMouthDetected);
		CorridorMouthEvidenceHoldRemaining = FMath::Max(
			CorridorForwardMouthEvidenceHoldRemaining,
			CorridorRearMouthEvidenceHoldRemaining);
		bCorridorMouthDetected = bCorridorForwardMouthDetected
			|| bCorridorRearMouthDetected;
	}
	else
	{
		CorridorForwardMouthEvidenceHoldRemaining = 0.0f;
		CorridorRearMouthEvidenceHoldRemaining = 0.0f;
		CorridorMouthEvidenceHoldRemaining = 0.0f;
		bCorridorForwardMouthDetected = bRawCorridorForwardMouthDetected;
		bCorridorRearMouthDetected = bRawCorridorRearMouthDetected;
		bCorridorMouthDetected = bRawCorridorMouthDetected;
	}
	UpdateMouthMixedState(SampleDeltaTime);

	int32 NearbyBlockedProbeCount = 0;
	const float NearbyWallDistance = FMath::Max(0.0f, PocketNearbyWallDistance);
	for (int32 ProbeIndex = 0; ProbeIndex < EffectiveProbeCount; ++ProbeIndex)
	{
		if (SpaceProbeBlocked[ProbeIndex] != 0
			&& ProbeDistances[ProbeIndex] <= NearbyWallDistance)
		{
			++NearbyBlockedProbeCount;
		}
	}
	EstimatedPocketBlockedFraction = static_cast<float>(NearbyBlockedProbeCount)
		/ static_cast<float>(EffectiveProbeCount);

	const float OpenProbeClearance = FMath::Clamp(
		PocketOpenProbeClearance,
		100.0f,
		EffectiveProbeDistance);
	int32 LongestOpenArcStart = INDEX_NONE;
	int32 LongestOpenArcProbeCount = 0;
	for (int32 StartIndex = 0; StartIndex < EffectiveProbeCount; ++StartIndex)
	{
		int32 OpenProbeCount = 0;
		while (OpenProbeCount < EffectiveProbeCount
			&& ProbeDistances[(StartIndex + OpenProbeCount) % EffectiveProbeCount] >= OpenProbeClearance)
		{
			++OpenProbeCount;
		}
		if (OpenProbeCount > LongestOpenArcProbeCount)
		{
			LongestOpenArcStart = StartIndex;
			LongestOpenArcProbeCount = OpenProbeCount;
		}
	}

	const float ProbeAngleStep = 360.0f / static_cast<float>(EffectiveProbeCount);
	EstimatedPocketOpenArc = LongestOpenArcProbeCount > 1
		? static_cast<float>(LongestOpenArcProbeCount - 1) * ProbeAngleStep
		: 0.0f;
	if (LongestOpenArcStart != INDEX_NONE && LongestOpenArcProbeCount > 0)
	{
		const float OpenArcCenterAngle = ProbeAngleStep
			* (static_cast<float>(LongestOpenArcStart)
				+ 0.5f * static_cast<float>(LongestOpenArcProbeCount - 1));
		EstimatedPocketOpenDirection = FVector(
			FMath::Cos(FMath::DegreesToRadians(OpenArcCenterAngle)),
			FMath::Sin(FMath::DegreesToRadians(OpenArcCenterAngle)),
			0.0f);
	}
	bHasValidSpaceAnalysis = true;
	SpaceAnalysisInvalidGraceRemaining = FMath::Max(0.0f, SpaceAnalysisInvalidGraceDuration);

	const float EnterWidth = FMath::Max(0.0f, CorridorEnterMaxWidth);
	const float ExitWidth = FMath::Max(EnterWidth, CorridorExitMinWidth);
	const float EnterRatio = FMath::Max(1.0f, CorridorEnterAspectRatio);
	const float ExitRatio = FMath::Clamp(CorridorExitAspectRatio, 1.0f, EnterRatio);
	const bool bCorridorEvidence = EstimatedCorridorAxisLength >= CorridorMinimumAxisLength
		&& EstimatedCorridorWidth <= EnterWidth
		&& EstimatedCorridorAspectRatio >= EnterRatio;
	const bool bOpenEvidence = EstimatedCorridorAxisLength < CorridorMinimumAxisLength
		|| EstimatedCorridorWidth >= ExitWidth
		|| EstimatedCorridorAspectRatio <= ExitRatio;
	const bool bCorridorExitShapeEvidence = EstimatedCorridorAxisLength >= CorridorMinimumAxisLength
		&& EstimatedCorridorWidth < ExitWidth
		&& EstimatedCorridorAspectRatio > ExitRatio;
	const float RequiredPocketBlockedFraction = CurrentSpaceMode == EBHCombatSpaceMode::Pocket
		? FMath::Clamp(PocketExitBlockedFraction, 0.0f, 1.0f)
		: FMath::Clamp(PocketEnterBlockedFraction, 0.0f, 1.0f);
	const float RequiredPocketOpenArc = CurrentSpaceMode == EBHCombatSpaceMode::Pocket
		? FMath::Clamp(PocketExitMinimumOpenArc, 0.0f, 360.0f)
		: FMath::Clamp(PocketEnterMinimumOpenArc, 0.0f, 360.0f);
	const bool bPocketEvidence = EstimatedPocketBlockedFraction >= RequiredPocketBlockedFraction
		&& EstimatedPocketOpenArc >= RequiredPocketOpenArc;
	const bool bUseCorridorEdgeExitThreshold = CurrentSpaceMode == EBHCombatSpaceMode::Pocket
		&& bCorridorEdgePocketActive;
	const float RequiredCorridorEdgeDistance = bUseCorridorEdgeExitThreshold
		? FMath::Max(0.0f, PocketCorridorEdgeExitDistance)
		: FMath::Max(0.0f, PocketCorridorEdgeEnterDistance);
	const float RequiredCorridorEdgeClearanceDifference = bUseCorridorEdgeExitThreshold
		? FMath::Max(0.0f, PocketCorridorEdgeExitClearanceDifference)
		: FMath::Max(0.0f, PocketCorridorEdgeEnterClearanceDifference);
	const bool bHasCorridorEdgeContext = CurrentSpaceMode == EBHCombatSpaceMode::Corridor
		|| bCorridorEdgePocketActive;
	const bool bCorridorEdgePocketEvidence = bHasCorridorEdgeContext
		&& EstimatedCorridorEdgeNearDistance <= RequiredCorridorEdgeDistance
		&& EstimatedCorridorEdgeClearanceDifference
			>= RequiredCorridorEdgeClearanceDifference;
	if (CurrentSpaceMode == EBHCombatSpaceMode::Corridor)
	{
		bCorridorEdgePocketActive = bCorridorEdgePocketEvidence;
	}
	else if (CurrentSpaceMode != EBHCombatSpaceMode::Pocket
		|| (bCorridorEdgePocketActive && !bCorridorEdgePocketEvidence))
	{
		bCorridorEdgePocketActive = false;
	}
	const bool bCenteredCorridorRecovery = CurrentSpaceMode == EBHCombatSpaceMode::Pocket
		&& bCorridorExitShapeEvidence
		&& !bCorridorEdgePocketEvidence;

	if (CurrentSpaceMode == EBHCombatSpaceMode::Corridor
		&& bCorridorMouthDetected)
	{
		// MouthMixed is a Corridor layout sub-state. Do not collapse the entire
		// formation into the open/pocket side while the target straddles a mouth.
		CandidateSpaceMode = EBHCombatSpaceMode::Corridor;
	}
	else if (bCorridorEdgePocketEvidence)
	{
		CandidateSpaceMode = EBHCombatSpaceMode::Pocket;
	}
	else if (bCorridorEvidence || bCenteredCorridorRecovery)
	{
		CandidateSpaceMode = EBHCombatSpaceMode::Corridor;
	}
	else if (bPocketEvidence)
	{
		CandidateSpaceMode = EBHCombatSpaceMode::Pocket;
	}
	else if (bOpenEvidence)
	{
		CandidateSpaceMode = EBHCombatSpaceMode::Open;
	}
	else
	{
		// The gap between enter/exit thresholds is an intentional hysteresis band.
		CandidateSpaceMode = CurrentSpaceMode;
	}

	if (CandidateSpaceMode == CurrentSpaceMode)
	{
		SpaceModeTransitionElapsed = 0.0f;
		UpdateCorridorFormationDirection();
		UpdatePocketFormationDirection();
		return;
	}

	SpaceModeTransitionElapsed += SampleDeltaTime;
	float RequiredDuration = FMath::Max(0.0f, CorridorExitDuration);
	if (CandidateSpaceMode == EBHCombatSpaceMode::Pocket)
	{
		RequiredDuration = FMath::Max(0.0f, PocketEnterDuration);
	}
	else if (CurrentSpaceMode == EBHCombatSpaceMode::Pocket)
	{
		RequiredDuration = FMath::Max(0.0f, PocketExitDuration);
	}
	else if (CandidateSpaceMode == EBHCombatSpaceMode::Corridor)
	{
		RequiredDuration = FMath::Max(0.0f, CorridorEnterDuration);
	}
	if (CurrentSpaceMode == EBHCombatSpaceMode::Corridor
		&& CandidateSpaceMode != EBHCombatSpaceMode::Corridor
		&& bCorridorMouthDetected)
	{
		RequiredDuration = FMath::Max(0.0f, MouthMixedEnterDuration);
	}
	if (SpaceModeTransitionElapsed >= RequiredDuration)
	{
		const EBHCombatSpaceMode PreviousMode = CurrentSpaceMode;
		CurrentSpaceMode = CandidateSpaceMode;
		SpaceModeTransitionElapsed = 0.0f;
		HandleCombatSpaceModeChanged(PreviousMode);
	}
}

int32 UCombatEngagementSlotComponent::GetActiveAttackSlotCount() const
{
	if (IsCorridorFormationActive())
	{
		return FMath::Clamp(ActiveCorridorAttackSlotCount, 0, AttackReservations.Num());
	}
	if (IsPocketFormationActive())
	{
		return FMath::Clamp(ActivePocketAttackSlotCount, 1, AttackReservations.Num());
	}
	return AttackReservations.Num();
}

int32 UCombatEngagementSlotComponent::GetCorridorLaneForRequester(AActor* Requester) const
{
	if (!IsCorridorFormationActive() || !Requester)
	{
		return INDEX_NONE;
	}

	int32 SlotIndex = INDEX_NONE;
	if (FindReservation(WaitReservations, Requester, SlotIndex)
		&& CorridorWaitRowLayout.IsValidIndex(SlotIndex))
	{
		return CorridorWaitRowLayout[SlotIndex].LaneIndex;
	}
	if (FindReservation(HoldingReservations, Requester, SlotIndex)
		&& CorridorHoldingRowLayout.IsValidIndex(SlotIndex))
	{
		return CorridorHoldingRowLayout[SlotIndex].LaneIndex;
	}
	return GetCorridorLaneIndex(Requester);
}

int32 UCombatEngagementSlotComponent::GetCorridorSideForRequester(AActor* Requester) const
{
	return IsCorridorFormationActive() ? GetCorridorSideIndex(Requester) : INDEX_NONE;
}

void UCombatEngagementSlotComponent::UpdateMouthMixedState(float SampleDeltaTime)
{
	if (CurrentSpaceMode != EBHCombatSpaceMode::Corridor)
	{
		MouthMixedEvidenceElapsed = 0.0f;
		PendingMouthMixedKind = EMouthMixedKind::None;
		if (bMouthMixedActive)
		{
			SetMouthMixedState(false);
		}
		return;
	}

	const auto ResolveKind = [](bool bForwardDetected, bool bRearDetected)
	{
		if (bForwardDetected && bRearDetected)
		{
			return EMouthMixedKind::DoubleMouth;
		}
		if (bForwardDetected)
		{
			// MouthSampleAxis follows CorridorFormationRearDirection, which is Side 0.
			return EMouthMixedKind::Side0Open;
		}
		return bRearDetected
			? EMouthMixedKind::Side1Open
			: EMouthMixedKind::None;
	};
	const EMouthMixedKind RawObservedKind = ResolveKind(
		bRawCorridorForwardMouthDetected,
		bRawCorridorRearMouthDetected);
	const EMouthMixedKind HeldObservedKind = ResolveKind(
		bCorridorForwardMouthDetected,
		bCorridorRearMouthDetected);

	if (bMouthMixedActive)
	{
		if (HeldObservedKind == EMouthMixedKind::None)
		{
			SetMouthMixedState(false);
			return;
		}
		if (RawObservedKind == EMouthMixedKind::None
			|| RawObservedKind == ActiveMouthMixedKind)
		{
			PendingMouthMixedKind = EMouthMixedKind::None;
			MouthMixedEvidenceElapsed = 0.0f;
			return;
		}

		if (PendingMouthMixedKind != RawObservedKind)
		{
			PendingMouthMixedKind = RawObservedKind;
			MouthMixedEvidenceElapsed = FMath::Max(0.0f, SampleDeltaTime);
			if (MouthMixedEvidenceElapsed >= FMath::Max(0.0f, MouthMixedKindChangeDuration))
			{
				SetMouthMixedState(true, RawObservedKind);
			}
			return;
		}
		MouthMixedEvidenceElapsed += FMath::Max(0.0f, SampleDeltaTime);
		if (MouthMixedEvidenceElapsed >= FMath::Max(0.0f, MouthMixedKindChangeDuration))
		{
			SetMouthMixedState(true, RawObservedKind);
		}
		return;
	}

	// Held evidence is only exit grace. It must never qualify a new MouthMixed
	// state after the raw detector has already gone away.
	if (RawObservedKind == EMouthMixedKind::None)
	{
		MouthMixedEvidenceElapsed = 0.0f;
		PendingMouthMixedKind = EMouthMixedKind::None;
		return;
	}
	if (PendingMouthMixedKind != RawObservedKind)
	{
		PendingMouthMixedKind = RawObservedKind;
		MouthMixedEvidenceElapsed = FMath::Max(0.0f, SampleDeltaTime);
		if (MouthMixedEvidenceElapsed >= FMath::Max(0.0f, MouthMixedEnterDuration))
		{
			SetMouthMixedState(true, RawObservedKind);
		}
		return;
	}

	MouthMixedEvidenceElapsed += FMath::Max(0.0f, SampleDeltaTime);
	if (MouthMixedEvidenceElapsed >= FMath::Max(0.0f, MouthMixedEnterDuration))
	{
		SetMouthMixedState(true, RawObservedKind);
	}
}

void UCombatEngagementSlotComponent::SetMouthMixedState(
	bool bNewActive,
	EMouthMixedKind NewKind)
{
	if (!bNewActive)
	{
		NewKind = EMouthMixedKind::None;
	}
	if (bMouthMixedActive == bNewActive && ActiveMouthMixedKind == NewKind)
	{
		return;
	}

	bMouthMixedActive = bNewActive;
	ActiveMouthMixedKind = NewKind;
	PendingMouthMixedKind = EMouthMixedKind::None;
	MouthMixedEvidenceElapsed = 0.0f;
	PendingCorridorAttackLayout.Reset();
	PendingCorridorCapacityElapsed = 0.0f;
	PendingCorridorWaitRowLayout.Reset();
	PendingCorridorHoldingRowLayout.Reset();
	PendingCorridorRowLayoutElapsed = 0.0f;
	++FormationRevision;
	NotifyAllReservedRequestersSlotChanged();
}

void UCombatEngagementSlotComponent::HandleCombatSpaceModeChanged(EBHCombatSpaceMode PreviousMode)
{
	if (PreviousMode == CurrentSpaceMode)
	{
		return;
	}
	const AActor* Owner = GetOwner();
	if (CurrentSpaceMode != EBHCombatSpaceMode::Corridor)
	{
		bMouthMixedActive = false;
		ActiveMouthMixedKind = EMouthMixedKind::None;
		PendingMouthMixedKind = EMouthMixedKind::None;
		MouthMixedEvidenceElapsed = 0.0f;
	}

	if (CurrentSpaceMode == EBHCombatSpaceMode::Corridor)
	{
		ActiveCorridorAttackLayout.Reset();
		PendingCorridorAttackLayout.Reset();
		CorridorAttackProbeLocations.Reset();
		CorridorAttackProbeValid.Reset();
		CorridorAttackProbeSelected.Reset();
		CorridorWaitRowLayout.Reset();
		CorridorHoldingRowLayout.Reset();
		PendingCorridorWaitRowLayout.Reset();
		PendingCorridorHoldingRowLayout.Reset();
		CorridorPendingRowStartDistance = 0.0f;
		PendingCorridorRowStartDistance = 0.0f;
		PendingCorridorRowLayoutElapsed = 0.0f;
		CorridorFormationRearDirection = ResolveCorridorRearDirection(EstimatedCorridorAxis);
		LastNotifiedCorridorDirection = CorridorFormationRearDirection;
		ActiveCorridorLaneCount = CalculateCorridorLaneCount(EstimatedCorridorWidth);
		AssignCorridorSideIndices(true);
		TArray<FCorridorAttackSlot> InitialAttackLayout;
		if (BuildCorridorAttackCandidateLayout(InitialAttackLayout))
		{
			CommitCorridorAttackLayout(MoveTemp(InitialAttackLayout));
		}
		else
		{
			ActiveCorridorAttackSlotCount = 0;
			DesiredCorridorAttackSlotCount = 0;
			ActiveCorridorSide0AttackSlotCount = 0;
			ActiveCorridorSide1AttackSlotCount = 0;
			DesiredCorridorSide0AttackSlotCount = 0;
			DesiredCorridorSide1AttackSlotCount = 0;
		}
		PendingCorridorCapacityElapsed = 0.0f;
		ReconcileCorridorAttackReservations();
	}
	else if (CurrentSpaceMode == EBHCombatSpaceMode::Pocket)
	{
		ActiveCorridorAttackLayout.Reset();
		PendingCorridorAttackLayout.Reset();
		CorridorAttackProbeLocations.Reset();
		CorridorAttackProbeValid.Reset();
		CorridorAttackProbeSelected.Reset();
		CorridorWaitRowLayout.Reset();
		CorridorHoldingRowLayout.Reset();
		PendingCorridorWaitRowLayout.Reset();
		PendingCorridorHoldingRowLayout.Reset();
		CorridorPendingRowStartDistance = 0.0f;
		PendingCorridorRowStartDistance = 0.0f;
		PendingCorridorRowLayoutElapsed = 0.0f;
		for (FEngagementQueueEntry& Entry : EngagementQueue)
		{
			Entry.CorridorSideIndex = INDEX_NONE;
		}
		PocketFormationDirection = EstimatedPocketOpenDirection.GetSafeNormal2D();
		if (PocketFormationDirection.IsNearlyZero())
		{
			PocketFormationDirection = Owner ? Owner->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
		}
		LastNotifiedPocketDirection = PocketFormationDirection;
		ActiveCorridorLaneCount = 1;
		ActiveCorridorAttackSlotCount = 1;
		DesiredCorridorAttackSlotCount = 1;
		ActiveCorridorSide0AttackSlotCount = 1;
		ActiveCorridorSide1AttackSlotCount = 0;
		DesiredCorridorSide0AttackSlotCount = 1;
		DesiredCorridorSide1AttackSlotCount = 0;
		DesiredPocketAttackSlotCount = CalculatePocketAttackSlotCount();
		const int32 PreviousPocketAttackSlotCount = FMath::Max(1, AttackReservations.Num());
		ActivePocketAttackSlotCount = FMath::Clamp(
			FMath::Max(DesiredPocketAttackSlotCount, GetLockedAttackReservationCount()),
			1,
			FMath::Max(1, AttackReservations.Num()));
		if (!ReconcilePocketAttackReservations())
		{
			ActivePocketAttackSlotCount = PreviousPocketAttackSlotCount;
			UE_LOG(
				LogProjectBH,
				Warning,
				TEXT("%s kept its previous Pocket Attack reservations because the new active row could not be projected."),
				GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"));
		}
		ActivatePocketRapidReform();
		ReformRingReservations(WaitReservations, EBHCombatSlotType::Wait);
		ReformRingReservations(HoldingReservations, EBHCombatSlotType::Holding);
	}
	else
	{
		ActiveCorridorAttackLayout.Reset();
		PendingCorridorAttackLayout.Reset();
		CorridorAttackProbeLocations.Reset();
		CorridorAttackProbeValid.Reset();
		CorridorAttackProbeSelected.Reset();
		CorridorWaitRowLayout.Reset();
		CorridorHoldingRowLayout.Reset();
		PendingCorridorWaitRowLayout.Reset();
		PendingCorridorHoldingRowLayout.Reset();
		CorridorPendingRowStartDistance = 0.0f;
		PendingCorridorRowStartDistance = 0.0f;
		PendingCorridorRowLayoutElapsed = 0.0f;
		for (FEngagementQueueEntry& Entry : EngagementQueue)
		{
			Entry.CorridorSideIndex = INDEX_NONE;
		}
		ActiveCorridorLaneCount = 1;
		ActiveCorridorAttackSlotCount = 1;
		DesiredCorridorAttackSlotCount = 1;
		ActiveCorridorSide0AttackSlotCount = 1;
		ActiveCorridorSide1AttackSlotCount = 0;
		DesiredCorridorSide0AttackSlotCount = 1;
		DesiredCorridorSide1AttackSlotCount = 0;
		ActivePocketAttackSlotCount = 1;
		DesiredPocketAttackSlotCount = 1;
	}

	++FormationRevision;
	NotifyAllReservedRequestersSlotChanged();
	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s changed combat space mode from %s to %s. Corridor sides:%d lanes:%d active attacks:%d (%d/%d) revision:%d"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		*StaticEnum<EBHCombatSpaceMode>()->GetNameStringByValue(static_cast<int64>(PreviousMode)),
		*StaticEnum<EBHCombatSpaceMode>()->GetNameStringByValue(static_cast<int64>(CurrentSpaceMode)),
		IsCorridorFormationActive() ? 2 : 0,
		ActiveCorridorLaneCount,
		GetActiveAttackSlotCount(),
		IsCorridorFormationActive() ? ActiveCorridorSide0AttackSlotCount : 0,
		IsCorridorFormationActive() ? ActiveCorridorSide1AttackSlotCount : 0,
		FormationRevision);
}

void UCombatEngagementSlotComponent::UpdateCorridorFormationDirection()
{
	if (!IsCorridorFormationActive())
	{
		return;
	}

	FVector AlignedAxis = EstimatedCorridorAxis.GetSafeNormal2D();
	if (AlignedAxis.IsNearlyZero())
	{
		return;
	}
	if (FVector::DotProduct(AlignedAxis, CorridorFormationRearDirection) < 0.0f)
	{
		AlignedAxis *= -1.0f;
	}

	const float FollowAlpha = FMath::Clamp(CorridorAxisFollowAlpha, 0.0f, 1.0f);
	CorridorFormationRearDirection = FMath::Lerp(
		CorridorFormationRearDirection,
		AlignedAxis,
		FollowAlpha).GetSafeNormal2D();
	if (CorridorFormationRearDirection.IsNearlyZero())
	{
		CorridorFormationRearDirection = AlignedAxis;
	}

	const float NotifyDotThreshold = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(CorridorFormationRepathAngle, 1.0f, 90.0f)));
	if (FVector::DotProduct(CorridorFormationRearDirection, LastNotifiedCorridorDirection)
		<= NotifyDotThreshold)
	{
		LastNotifiedCorridorDirection = CorridorFormationRearDirection;
		++FormationRevision;
		NotifyAllReservedRequestersSlotChanged();
	}
}

void UCombatEngagementSlotComponent::UpdatePocketFormationDirection()
{
	if (!IsPocketFormationActive())
	{
		return;
	}

	FVector OpenDirection = EstimatedPocketOpenDirection.GetSafeNormal2D();
	if (OpenDirection.IsNearlyZero())
	{
		return;
	}

	const float FollowAlpha = FMath::Clamp(PocketDirectionFollowAlpha, 0.0f, 1.0f);
	PocketFormationDirection = FMath::Lerp(
		PocketFormationDirection,
		OpenDirection,
		FollowAlpha).GetSafeNormal2D();
	if (PocketFormationDirection.IsNearlyZero())
	{
		PocketFormationDirection = OpenDirection;
	}

	const float NotifyDotThreshold = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(PocketFormationRepathAngle, 1.0f, 90.0f)));
	if (FVector::DotProduct(PocketFormationDirection, LastNotifiedPocketDirection)
		<= NotifyDotThreshold)
	{
		LastNotifiedPocketDirection = PocketFormationDirection;
		ActivatePocketRapidReform();
		ReformRingReservations(WaitReservations, EBHCombatSlotType::Wait);
		ReformRingReservations(HoldingReservations, EBHCombatSlotType::Holding);
		++FormationRevision;
		NotifyAllReservedRequestersSlotChanged();
	}
}

void UCombatEngagementSlotComponent::RefreshCorridorFormationCapacity(float DeltaTime)
{
	if (!IsCorridorFormationActive())
	{
		return;
	}

	RefreshCorridorAttackLayoutWorldLocations(ActiveCorridorAttackLayout);
	TArray<FCorridorAttackSlot> CandidateLayout;
	if (!BuildCorridorAttackCandidateLayout(CandidateLayout))
	{
		// The navigation query itself is temporarily unavailable. Keep the last
		// committed layout instead of destructively reassigning reservations.
		return;
	}

	DesiredCorridorAttackSlotCount = CandidateLayout.Num();
	DesiredCorridorSide0AttackSlotCount = 0;
	DesiredCorridorSide1AttackSlotCount = 0;
	for (const FCorridorAttackSlot& Candidate : CandidateLayout)
	{
		(Candidate.SideIndex == 0
			? DesiredCorridorSide0AttackSlotCount
			: DesiredCorridorSide1AttackSlotCount)++;
	}

	if (AreCorridorAttackLayoutsEquivalent(
		ActiveCorridorAttackLayout,
		CandidateLayout))
	{
		ActiveCorridorAttackLayout = MoveTemp(CandidateLayout);
		RefreshCorridorAttackSideCounts();
		PendingCorridorAttackLayout.Reset();
		PendingCorridorCapacityElapsed = 0.0f;
		return;
	}
	if (ActiveCorridorAttackLayout.IsEmpty())
	{
		CommitCorridorAttackLayout(MoveTemp(CandidateLayout));
		PendingCorridorAttackLayout.Reset();
		PendingCorridorCapacityElapsed = 0.0f;
		ReconcileCorridorAttackReservations();
		++FormationRevision;
		NotifyAllReservedRequestersSlotChanged();
		return;
	}

	// Never reinterpret a live attacking/recovering reservation. The existing
	// layout remains pinned until all attack locks have been released.
	if (GetLockedAttackReservationCount() > 0)
	{
		PendingCorridorAttackLayout.Reset();
		PendingCorridorCapacityElapsed = 0.0f;
		return;
	}

	if (!AreCorridorAttackLayoutsEquivalent(
		PendingCorridorAttackLayout,
		CandidateLayout))
	{
		PendingCorridorAttackLayout = MoveTemp(CandidateLayout);
		PendingCorridorCapacityElapsed = 0.0f;
	}
	else
	{
		PendingCorridorAttackLayout = MoveTemp(CandidateLayout);
		PendingCorridorCapacityElapsed += FMath::Max(0.0f, DeltaTime);
	}
	if (PendingCorridorCapacityElapsed < FMath::Max(0.0f, CorridorCapacityCommitDelay))
	{
		return;
	}

	CommitCorridorAttackLayout(MoveTemp(PendingCorridorAttackLayout));
	PendingCorridorCapacityElapsed = 0.0f;
	ReconcileCorridorAttackReservations();
	++FormationRevision;
	NotifyAllReservedRequestersSlotChanged();
}

void UCombatEngagementSlotComponent::RefreshCorridorRowLayout(float DeltaTime)
{
	if (!IsCorridorFormationActive())
	{
		CorridorWaitRowLayout.Reset();
		CorridorHoldingRowLayout.Reset();
		PendingCorridorWaitRowLayout.Reset();
		PendingCorridorHoldingRowLayout.Reset();
		CorridorPendingRowStartDistance = 0.0f;
		PendingCorridorRowStartDistance = 0.0f;
		PendingCorridorRowLayoutElapsed = 0.0f;
		return;
	}
	TArray<FCorridorRowSlot> CandidateWaitLayout;
	TArray<FCorridorRowSlot> CandidateHoldingLayout;
	float CandidatePendingStartDistance = 0.0f;
	if (!BuildCorridorRowLayouts(
		CandidateWaitLayout,
		CandidateHoldingLayout,
		CandidatePendingStartDistance))
	{
		// A transient NavMesh query failure must not tear down the committed queue.
		return;
	}

	const bool bHasCommittedLayout = !CorridorWaitRowLayout.IsEmpty()
		|| !CorridorHoldingRowLayout.IsEmpty();
	if (!bHasCommittedLayout)
	{
		CorridorWaitRowLayout = MoveTemp(CandidateWaitLayout);
		CorridorHoldingRowLayout = MoveTemp(CandidateHoldingLayout);
		CorridorPendingRowStartDistance = CandidatePendingStartDistance;
		PendingCorridorWaitRowLayout.Reset();
		PendingCorridorHoldingRowLayout.Reset();
		PendingCorridorRowLayoutElapsed = 0.0f;
		RepackAllCorridorQueueLayers(false);
		++FormationRevision;
		NotifyAllReservedRequestersSlotChanged();
		return;
	}

	const bool bTopologyUnchanged = AreCorridorRowLayoutsEquivalent(
		CorridorWaitRowLayout,
		CandidateWaitLayout)
		&& AreCorridorRowLayoutsEquivalent(
			CorridorHoldingRowLayout,
			CandidateHoldingLayout);
	if (bTopologyUnchanged)
	{
		// Keep exact positions responsive while retaining the same slot identities.
		CorridorWaitRowLayout = MoveTemp(CandidateWaitLayout);
		CorridorHoldingRowLayout = MoveTemp(CandidateHoldingLayout);
		CorridorPendingRowStartDistance = CandidatePendingStartDistance;
		PendingCorridorWaitRowLayout.Reset();
		PendingCorridorHoldingRowLayout.Reset();
		PendingCorridorRowStartDistance = CandidatePendingStartDistance;
		PendingCorridorRowLayoutElapsed = 0.0f;
		return;
	}

	const bool bMatchesPendingTopology = AreCorridorRowLayoutsEquivalent(
		PendingCorridorWaitRowLayout,
		CandidateWaitLayout)
		&& AreCorridorRowLayoutsEquivalent(
			PendingCorridorHoldingRowLayout,
			CandidateHoldingLayout);
	if (!bMatchesPendingTopology)
	{
		PendingCorridorWaitRowLayout = MoveTemp(CandidateWaitLayout);
		PendingCorridorHoldingRowLayout = MoveTemp(CandidateHoldingLayout);
		PendingCorridorRowStartDistance = CandidatePendingStartDistance;
		PendingCorridorRowLayoutElapsed = 0.0f;
		return;
	}

	// Refresh the candidate's world positions while its topology proves stable.
	PendingCorridorWaitRowLayout = MoveTemp(CandidateWaitLayout);
	PendingCorridorHoldingRowLayout = MoveTemp(CandidateHoldingLayout);
	PendingCorridorRowStartDistance = CandidatePendingStartDistance;
	PendingCorridorRowLayoutElapsed += FMath::Max(0.0f, DeltaTime);
	if (PendingCorridorRowLayoutElapsed
		< FMath::Max(0.0f, CorridorRowLayoutCommitDelay))
	{
		return;
	}

	CorridorWaitRowLayout = MoveTemp(PendingCorridorWaitRowLayout);
	CorridorHoldingRowLayout = MoveTemp(PendingCorridorHoldingRowLayout);
	CorridorPendingRowStartDistance = PendingCorridorRowStartDistance;
	PendingCorridorRowLayoutElapsed = 0.0f;
	RepackAllCorridorQueueLayers(false);
	++FormationRevision;
	NotifyAllReservedRequestersSlotChanged();
}

bool UCombatEngagementSlotComponent::BuildCorridorRowLayouts(
	TArray<FCorridorRowSlot>& OutWaitLayout,
	TArray<FCorridorRowSlot>& OutHoldingLayout,
	float& OutPendingStartDistance) const
{
	OutWaitLayout.Reset();
	OutHoldingLayout.Reset();
	const float LayerGap = FMath::Max(0.0f, CorridorLayerGap);
	const float WaitStartDistance = FMath::Max(0.0f, AttackRingRadius) + LayerGap;
	float WaitLastRowDistance = WaitStartDistance;
	if (!BuildCorridorLayerLayout(
		EBHCombatSlotType::Wait,
		WaitStartDistance,
		WaitReservations.Num(),
		OutWaitLayout,
		WaitLastRowDistance))
	{
		return false;
	}

	const float HoldingStartDistance = WaitLastRowDistance + LayerGap;
	float HoldingLastRowDistance = HoldingStartDistance;
	if (!BuildCorridorLayerLayout(
		EBHCombatSlotType::Holding,
		HoldingStartDistance,
		HoldingReservations.Num(),
		OutHoldingLayout,
		HoldingLastRowDistance))
	{
		return false;
	}

	OutPendingStartDistance = HoldingLastRowDistance + LayerGap;
	return true;
}

bool UCombatEngagementSlotComponent::BuildCorridorLayerLayout(
	EBHCombatSlotType SlotType,
	float StartDistance,
	int32 RequiredSlotCount,
	TArray<FCorridorRowSlot>& OutLayout,
	float& OutLastRowDistance) const
{
	OutLayout.Reset();
	OutLastRowDistance = StartDistance;
	if (SlotType != EBHCombatSlotType::Wait
		&& SlotType != EBHCombatSlotType::Holding
		&& SlotType != EBHCombatSlotType::Pending)
	{
		return false;
	}
	if (RequiredSlotCount <= 0)
	{
		return true;
	}

	const float RowSpacing = FMath::Max(1.0f, CorridorRowSpacing);
	const int32 ExtraBalanceRows = FMath::Max(0, CorridorSideBalanceSearchExtraRows);
	const int32 MaximumRowsToInspect = FMath::Max(
		8,
		RequiredSlotCount * 2 + ExtraBalanceRows);
	const int32 PreferredSideCounts[2] =
	{
		(RequiredSlotCount + 1) / 2,
		RequiredSlotCount / 2
	};
	TArray<FCorridorRowSlot> SideCandidates[2];
	int32 FirstTotalCapacityRow = INDEX_NONE;
	for (int32 RowIndex = 0;
		RowIndex < MaximumRowsToInspect;
		++RowIndex)
	{
		const float RowDistance = StartDistance
			+ static_cast<float>(RowIndex) * RowSpacing;
		for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
		{
			TArray<FCorridorRowSlot> RowCandidates;
			BuildCorridorRowSlots(SideIndex, RowIndex, RowDistance, RowCandidates);
			SideCandidates[SideIndex].Append(MoveTemp(RowCandidates));
		}

		if (SideCandidates[0].Num() >= PreferredSideCounts[0]
			&& SideCandidates[1].Num() >= PreferredSideCounts[1])
		{
			break;
		}

		if (FirstTotalCapacityRow == INDEX_NONE
			&& SideCandidates[0].Num() + SideCandidates[1].Num()
				>= RequiredSlotCount)
		{
			FirstTotalCapacityRow = RowIndex;
		}
		if (FirstTotalCapacityRow != INDEX_NONE
			&& RowIndex - FirstTotalCapacityRow >= ExtraBalanceRows)
		{
			break;
		}
	}

	if (SideCandidates[0].Num() + SideCandidates[1].Num() < RequiredSlotCount)
	{
		return false;
	}

	int32 SideQuotas[2] =
	{
		FMath::Min(PreferredSideCounts[0], SideCandidates[0].Num()),
		FMath::Min(PreferredSideCounts[1], SideCandidates[1].Num())
	};
	int32 RemainingQuota = RequiredSlotCount - SideQuotas[0] - SideQuotas[1];
	while (RemainingQuota > 0)
	{
		const int32 Side0Remaining = SideCandidates[0].Num() - SideQuotas[0];
		const int32 Side1Remaining = SideCandidates[1].Num() - SideQuotas[1];
		const int32 SpillSideIndex = Side0Remaining >= Side1Remaining ? 0 : 1;
		if ((SpillSideIndex == 0 ? Side0Remaining : Side1Remaining) <= 0)
		{
			return false;
		}
		++SideQuotas[SpillSideIndex];
		--RemainingQuota;
	}

	int32 SideCursors[2] = { 0, 0 };
	const int32 FirstSideIndex = static_cast<int32>(SlotType) % 2;
	while (OutLayout.Num() < RequiredSlotCount)
	{
		bool bAddedSlot = false;
		for (int32 SidePass = 0; SidePass < 2; ++SidePass)
		{
			const int32 SideIndex = (FirstSideIndex + SidePass) % 2;
			if (SideCursors[SideIndex] >= SideQuotas[SideIndex])
			{
				continue;
			}
			const FCorridorRowSlot& SelectedSlot =
				SideCandidates[SideIndex][SideCursors[SideIndex]++];
			OutLayout.Add(SelectedSlot);
			OutLastRowDistance = FMath::Max(
				OutLastRowDistance,
				StartDistance + static_cast<float>(SelectedSlot.RowIndex) * RowSpacing);
			bAddedSlot = true;
		}
		if (!bAddedSlot)
		{
			return false;
		}
	}

	return true;
}

bool UCombatEngagementSlotComponent::BuildCorridorRowSlots(
	int32 SideIndex,
	int32 RowIndex,
	float LongitudinalDistance,
	TArray<FCorridorRowSlot>& OutRowSlots) const
{
	OutRowSlots.Reset();
	if (IsMouthMixedFanSide(SideIndex))
	{
		return BuildMouthMixedFanRowSlots(
			SideIndex,
			RowIndex,
			LongitudinalDistance,
			OutRowSlots);
	}

	const AActor* Owner = GetOwner();
	const FVector AxisDirection = CorridorFormationRearDirection.GetSafeNormal2D();
	if (!Owner || !IsCorridorFormationActive()
		|| !FMath::IsWithin(SideIndex, 0, 2)
		|| AxisDirection.IsNearlyZero())
	{
		return false;
	}

	const FVector SideDirection = SideIndex == 0 ? AxisDirection : -AxisDirection;
	const float AnchorLag = FMath::Max(0.0f, FVector::DotProduct(
		EngagementAnchorLocation - Owner->GetActorLocation(),
		SideDirection));
	const FVector OuterCenter = bMouthMixedActive
		? EngagementAnchorLocation
		: Owner->GetActorLocation() + SideDirection * AnchorLag;
	const FVector DesiredRowCenter = OuterCenter
		+ SideDirection * FMath::Max(0.0f, LongitudinalDistance);
	float MeasuredWidth = 0.0f;
	FVector RowCenter;
	FVector Side0End;
	FVector Side1End;
	if (!MeasureCorridorCrossSection(
		DesiredRowCenter,
		AxisDirection,
		CorridorRowProbeHalfWidth,
		MeasuredWidth,
		RowCenter,
		Side0End,
		Side1End))
	{
		return false;
	}

	const float AgentRadius = FMath::Max(1.0f, CorridorAgentRadius);
	const float Spacing = FMath::Max(1.0f, CorridorSlotSpacing);
	const FVector CrossDirection(-AxisDirection.Y, AxisDirection.X, 0.0f);
	const float Side0Distance = FVector::Dist2D(RowCenter, Side0End);
	const float Side1Distance = FVector::Dist2D(RowCenter, Side1End);
	const float MinimumOffset = -Side1Distance + AgentRadius;
	const float MaximumOffset = Side0Distance - AgentRadius;
	const float UsableWidth = MaximumOffset - MinimumOffset;
	if (UsableWidth < 0.0f || MeasuredWidth < 2.0f * AgentRadius)
	{
		return false;
	}

	const int32 DesiredLaneCount = FMath::Clamp(
		1 + FMath::FloorToInt(UsableWidth / Spacing),
		1,
		FMath::Max(1, CorridorRowMaximumLaneCount));
	const float UsableCenterOffset = 0.5f * (MinimumOffset + MaximumOffset);
	const float ProjectionToleranceSquared = FMath::Square(
		FMath::Max(0.0f, CorridorRowProjectionTolerance));
	for (int32 LaneIndex = 0; LaneIndex < DesiredLaneCount; ++LaneIndex)
	{
		const float CenteredLaneOffset = (static_cast<float>(LaneIndex)
			- 0.5f * static_cast<float>(DesiredLaneCount - 1)) * Spacing;
		const FVector DesiredLocation = RowCenter
			+ CrossDirection * (UsableCenterOffset + CenteredLaneOffset);
		FVector ProjectedLocation;
		if (!ProjectToNavigation(DesiredLocation, ProjectedLocation)
			|| FVector::DistSquared2D(DesiredLocation, ProjectedLocation)
				> ProjectionToleranceSquared)
		{
			continue;
		}

		FCorridorRowSlot& RowSlot = OutRowSlots.AddDefaulted_GetRef();
		RowSlot.WorldLocation = ProjectedLocation;
		RowSlot.SideIndex = SideIndex;
		RowSlot.RowIndex = RowIndex;
		RowSlot.LaneIndex = LaneIndex;
	}

	const int32 ValidLaneCount = OutRowSlots.Num();
	for (FCorridorRowSlot& RowSlot : OutRowSlots)
	{
		RowSlot.LaneCount = ValidLaneCount;
	}
	// Queue order inside a row is center-out, not permanently left-to-right.
	OutRowSlots.Sort([DesiredLaneCount](const FCorridorRowSlot& Left, const FCorridorRowSlot& Right)
	{
		const float CenterLane = 0.5f * static_cast<float>(DesiredLaneCount - 1);
		const float LeftCenterDistance = FMath::Abs(static_cast<float>(Left.LaneIndex) - CenterLane);
		const float RightCenterDistance = FMath::Abs(static_cast<float>(Right.LaneIndex) - CenterLane);
		if (!FMath::IsNearlyEqual(LeftCenterDistance, RightCenterDistance))
		{
			return LeftCenterDistance < RightCenterDistance;
		}
		return Left.LaneIndex < Right.LaneIndex;
	});
	return !OutRowSlots.IsEmpty();
}

bool UCombatEngagementSlotComponent::BuildMouthMixedFanRowSlots(
	int32 SideIndex,
	int32 RowIndex,
	float LongitudinalDistance,
	TArray<FCorridorRowSlot>& OutRowSlots) const
{
	OutRowSlots.Reset();
	const FVector AxisDirection = CorridorFormationRearDirection.GetSafeNormal2D();
	if (!GetOwner()
		|| !IsMouthMixedFanSide(SideIndex)
		|| AxisDirection.IsNearlyZero())
	{
		return false;
	}

	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	FVector ProjectedCenter;
	if (!NavigationSystem
		|| !ProjectToNavigation(EngagementAnchorLocation, ProjectedCenter))
	{
		return false;
	}

	const FVector OpenDirection = SideIndex == 0 ? AxisDirection : -AxisDirection;
	const float Radius = FMath::Max(1.0f, LongitudinalDistance);
	const float HalfAngle = FMath::Clamp(PocketMaximumArcHalfAngle, 10.0f, 89.0f);
	const float Spacing = FMath::Min(
		FMath::Max(1.0f, CorridorSlotSpacing),
		2.0f * Radius);
	const float StepAngle = FMath::RadiansToDegrees(2.0f * FMath::Asin(FMath::Clamp(
		Spacing / (2.0f * Radius),
		0.0f,
		1.0f)));
	const int32 DesiredLaneCount = FMath::Clamp(
		1 + FMath::FloorToInt((2.0f * HalfAngle) / FMath::Max(1.0f, StepAngle)),
		1,
		FMath::Max(1, CorridorRowMaximumLaneCount));
	const float FittedStepAngle = DesiredLaneCount > 1
		? FMath::Min(
			StepAngle,
			(2.0f * HalfAngle) / static_cast<float>(DesiredLaneCount - 1))
		: 0.0f;
	const float ProjectionToleranceSquared = FMath::Square(
		FMath::Max(0.0f, CorridorRowProjectionTolerance));
	const float MinimumSpacingSquared = FMath::Square(
		FMath::Max(1.0f, CorridorSlotSpacing) * 0.75f);
	for (int32 LaneIndex = 0; LaneIndex < DesiredLaneCount; ++LaneIndex)
	{
		const float CenteredLane = static_cast<float>(LaneIndex)
			- 0.5f * static_cast<float>(DesiredLaneCount - 1);
		const float DesiredAngle = FMath::Clamp(
			CenteredLane * FittedStepAngle,
			-HalfAngle,
			HalfAngle);
		for (int32 AttemptIndex = 0; AttemptIndex < 5; ++AttemptIndex)
		{
			const float AngleScale = 1.0f - static_cast<float>(AttemptIndex) / 5.0f;
			const FVector SlotDirection = OpenDirection.RotateAngleAxis(
				DesiredAngle * AngleScale,
				FVector::UpVector).GetSafeNormal2D();
			const FVector DesiredLocation = EngagementAnchorLocation
				+ SlotDirection * Radius;
			FVector ProjectedLocation;
			FVector RaycastHitLocation;
			if (!ProjectToNavigation(DesiredLocation, ProjectedLocation)
				|| FVector::DistSquared2D(DesiredLocation, ProjectedLocation)
					> ProjectionToleranceSquared
				|| NavigationSystem->NavigationRaycast(
					World,
					ProjectedCenter,
					ProjectedLocation,
					RaycastHitLocation))
			{
				continue;
			}

			bool bOverlapsExistingLane = false;
			for (const FCorridorRowSlot& ExistingSlot : OutRowSlots)
			{
				if (FVector::DistSquared2D(
					ExistingSlot.WorldLocation,
					ProjectedLocation) < MinimumSpacingSquared)
				{
					bOverlapsExistingLane = true;
					break;
				}
			}
			if (bOverlapsExistingLane)
			{
				continue;
			}

			FCorridorRowSlot& RowSlot = OutRowSlots.AddDefaulted_GetRef();
			RowSlot.WorldLocation = ProjectedLocation;
			RowSlot.SideIndex = SideIndex;
			RowSlot.RowIndex = RowIndex;
			RowSlot.LaneIndex = LaneIndex;
			break;
		}
	}

	const int32 ValidLaneCount = OutRowSlots.Num();
	for (FCorridorRowSlot& RowSlot : OutRowSlots)
	{
		RowSlot.LaneCount = ValidLaneCount;
	}
	OutRowSlots.Sort([DesiredLaneCount](const FCorridorRowSlot& Left, const FCorridorRowSlot& Right)
	{
		const float CenterLane = 0.5f * static_cast<float>(DesiredLaneCount - 1);
		const float LeftCenterDistance = FMath::Abs(static_cast<float>(Left.LaneIndex) - CenterLane);
		const float RightCenterDistance = FMath::Abs(static_cast<float>(Right.LaneIndex) - CenterLane);
		if (!FMath::IsNearlyEqual(LeftCenterDistance, RightCenterDistance))
		{
			return LeftCenterDistance < RightCenterDistance;
		}
		return Left.LaneIndex < Right.LaneIndex;
	});
	return !OutRowSlots.IsEmpty();
}

bool UCombatEngagementSlotComponent::IsMouthMixedFanSide(int32 SideIndex) const
{
	if (!bMouthMixedActive || !FMath::IsWithin(SideIndex, 0, 2))
	{
		return false;
	}

	switch (ActiveMouthMixedKind)
	{
	case EMouthMixedKind::Side0Open:
		return SideIndex == 0;
	case EMouthMixedKind::Side1Open:
		return SideIndex == 1;
	case EMouthMixedKind::DoubleMouth:
		return true;
	default:
		return false;
	}
}

bool UCombatEngagementSlotComponent::AreCorridorRowLayoutsEquivalent(
	const TArray<FCorridorRowSlot>& LayoutA,
	const TArray<FCorridorRowSlot>& LayoutB) const
{
	if (LayoutA.Num() != LayoutB.Num())
	{
		return false;
	}
	for (int32 SlotIndex = 0; SlotIndex < LayoutA.Num(); ++SlotIndex)
	{
		const FCorridorRowSlot& SlotA = LayoutA[SlotIndex];
		const FCorridorRowSlot& SlotB = LayoutB[SlotIndex];
		if (SlotA.SideIndex != SlotB.SideIndex
			|| SlotA.RowIndex != SlotB.RowIndex
			|| SlotA.LaneIndex != SlotB.LaneIndex
			|| SlotA.LaneCount != SlotB.LaneCount)
		{
			return false;
		}
	}
	return true;
}

bool UCombatEngagementSlotComponent::GetCorridorDynamicSlotWorldLocation(
	EBHCombatSlotType SlotType,
	int32 SlotIndex,
	FVector& OutWorldLocation) const
{
	if (SlotIndex < 0 || !IsCorridorFormationActive())
	{
		return false;
	}

	const TArray<FCorridorRowSlot>* Layout = nullptr;
	if (SlotType == EBHCombatSlotType::Wait)
	{
		Layout = &CorridorWaitRowLayout;
	}
	else if (SlotType == EBHCombatSlotType::Holding)
	{
		Layout = &CorridorHoldingRowLayout;
	}
	if (Layout)
	{
		if (!Layout->IsValidIndex(SlotIndex))
		{
			return false;
		}
		OutWorldLocation = (*Layout)[SlotIndex].WorldLocation;
		return true;
	}
	if (SlotType != EBHCombatSlotType::Pending)
	{
		return false;
	}

	TArray<FCorridorRowSlot> PendingLayout;
	float IgnoredLastRowDistance = CorridorPendingRowStartDistance;
	if (!BuildCorridorLayerLayout(
		EBHCombatSlotType::Pending,
		CorridorPendingRowStartDistance,
		SlotIndex + 1,
		PendingLayout,
		IgnoredLastRowDistance)
		|| !PendingLayout.IsValidIndex(SlotIndex))
	{
		return false;
	}
	OutWorldLocation = PendingLayout[SlotIndex].WorldLocation;
	return true;
}

void UCombatEngagementSlotComponent::RefreshPocketFormationCapacity()
{
	if (!IsPocketFormationActive())
	{
		return;
	}

	DesiredPocketAttackSlotCount = CalculatePocketAttackSlotCount();
	const int32 RequiredAttackCount = FMath::Clamp(
		FMath::Max(DesiredPocketAttackSlotCount, GetLockedAttackReservationCount()),
		1,
		FMath::Max(1, AttackReservations.Num()));
	if (RequiredAttackCount == ActivePocketAttackSlotCount)
	{
		return;
	}

	const int32 PreviousAttackCount = ActivePocketAttackSlotCount;
	ActivePocketAttackSlotCount = RequiredAttackCount;
	if (!ReconcilePocketAttackReservations())
	{
		ActivePocketAttackSlotCount = PreviousAttackCount;
		return;
	}
	++FormationRevision;
	NotifyAllReservedRequestersSlotChanged();
}

void UCombatEngagementSlotComponent::ReconcileCorridorAttackReservations()
{
	if (!IsCorridorFormationActive())
	{
		return;
	}

	struct FLockedAttackReservation
	{
		TWeakObjectPtr<AActor> Requester;
		int32 PreviousSlotIndex = INDEX_NONE;
	};

	TArray<FLockedAttackReservation, TInlineAllocator<8>> LockedRequesters;
	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<8>> OtherRequesters;
	for (int32 SlotIndex = 0; SlotIndex < AttackReservations.Num(); ++SlotIndex)
	{
		TWeakObjectPtr<AActor>& Reservation = AttackReservations[SlotIndex];
		AActor* Requester = Reservation.Get();
		if (Requester)
		{
			const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
			const bool bLocked = Enemy
				&& (Enemy->GetCombatState() == EBHEnemyCombatState::Attacking
					|| Enemy->GetCombatState() == EBHEnemyCombatState::Recovering);
			if (bLocked)
			{
				FLockedAttackReservation& LockedReservation = LockedRequesters.AddDefaulted_GetRef();
				LockedReservation.Requester = Requester;
				LockedReservation.PreviousSlotIndex = SlotIndex;
			}
			else
			{
				OtherRequesters.Add(Requester);
			}
		}
		Reservation.Reset();
	}

	for (const FLockedAttackReservation& LockedReservation : LockedRequesters)
	{
		AActor* Requester = LockedReservation.Requester.Get();
		if (!Requester)
		{
			continue;
		}

		int32 ReservedIndex = AttackReservations.IsValidIndex(LockedReservation.PreviousSlotIndex)
			&& LockedReservation.PreviousSlotIndex < ActiveCorridorAttackSlotCount
			&& !AttackReservations[LockedReservation.PreviousSlotIndex].IsValid()
			? LockedReservation.PreviousSlotIndex
			: INDEX_NONE;
		const int32 PreferredChannel = GetCorridorQueueChannelIndex(Requester);
		for (int32 SlotIndex = 0; SlotIndex < ActiveCorridorAttackSlotCount; ++SlotIndex)
		{
			if (ReservedIndex == INDEX_NONE
				&& !AttackReservations[SlotIndex].IsValid()
				&& GetCorridorAttackSlotChannelIndex(SlotIndex) == PreferredChannel)
			{
				ReservedIndex = SlotIndex;
				break;
			}
		}
		if (ReservedIndex == INDEX_NONE)
		{
			const int32 StableSide = GetCorridorSideIndex(Requester);
			for (int32 SlotIndex = 0; SlotIndex < ActiveCorridorAttackSlotCount; ++SlotIndex)
			{
				if (!AttackReservations[SlotIndex].IsValid()
					&& GetCorridorAttackSlotSideIndex(SlotIndex) == StableSide)
				{
					ReservedIndex = SlotIndex;
					break;
				}
			}
		}
		if (ReservedIndex == INDEX_NONE)
		{
			for (int32 SlotIndex = 0; SlotIndex < ActiveCorridorAttackSlotCount; ++SlotIndex)
			{
				if (!AttackReservations[SlotIndex].IsValid())
				{
					ReservedIndex = SlotIndex;
					break;
				}
			}
		}
		if (AttackReservations.IsValidIndex(ReservedIndex))
		{
			AttackReservations[ReservedIndex] = Requester;
			UpdateCorridorSideForAttackReservation(Requester, ReservedIndex);
		}
	}

	for (int32 SlotIndex = 0;
		SlotIndex < ActiveCorridorAttackSlotCount && !OtherRequesters.IsEmpty();
		++SlotIndex)
	{
		if (AttackReservations[SlotIndex].IsValid())
		{
			continue;
		}

		int32 BestRequesterIndex = INDEX_NONE;
		float BestPathScore = TNumericLimits<float>::Max();
		bool bBestUsesPreferredSide = false;
		bool bBestUsesPreferredLane = false;
		FVector SlotLocation;
		if (!GetCorridorSlotWorldLocation(EBHCombatSlotType::Attack, SlotIndex, SlotLocation))
		{
			continue;
		}
		for (int32 RequesterIndex = 0; RequesterIndex < OtherRequesters.Num(); ++RequesterIndex)
		{
			AActor* Requester = OtherRequesters[RequesterIndex].Get();
			if (!Requester)
			{
				continue;
			}
			if (!CanRequesterOccupyAttackSlot(Requester, SlotIndex))
			{
				continue;
			}
			if (!IsCorridorAttackSlotOnRequesterSide(Requester, SlotIndex))
			{
				continue;
			}

			float PathScore = 0.0f;
			if (!GetNavigationPathScore(Requester, SlotLocation, PathScore))
			{
				continue;
			}

			const bool bUsesPreferredSide = GetCorridorSideIndex(Requester)
				== GetCorridorAttackSlotSideIndex(SlotIndex);
			const bool bUsesPreferredLane = GetCorridorQueueChannelIndex(Requester)
				== GetCorridorAttackSlotChannelIndex(SlotIndex);
			if (BestRequesterIndex == INDEX_NONE
				|| (bUsesPreferredSide && !bBestUsesPreferredSide)
				|| (bUsesPreferredSide == bBestUsesPreferredSide
					&& bUsesPreferredLane && !bBestUsesPreferredLane)
				|| (bUsesPreferredSide == bBestUsesPreferredSide
					&& bUsesPreferredLane == bBestUsesPreferredLane
					&& PathScore < BestPathScore))
			{
				BestPathScore = PathScore;
				bBestUsesPreferredSide = bUsesPreferredSide;
				bBestUsesPreferredLane = bUsesPreferredLane;
				BestRequesterIndex = RequesterIndex;
			}
		}
		if (BestRequesterIndex == INDEX_NONE)
		{
			continue;
		}

		AActor* AssignedRequester = OtherRequesters[BestRequesterIndex].Get();
		AttackReservations[SlotIndex] = AssignedRequester;
		UpdateCorridorSideForAttackReservation(AssignedRequester, SlotIndex);
		OtherRequesters.RemoveAtSwap(BestRequesterIndex, 1, EAllowShrinking::No);
	}

	for (const TWeakObjectPtr<AActor>& RequesterPtr : OtherRequesters)
	{
		AActor* Requester = RequesterPtr.Get();
		if (!Requester)
		{
			continue;
		}

		if (!TryReserveSlot(Requester, EBHCombatSlotType::Wait)
			&& !TryReserveSlot(Requester, EBHCombatSlotType::Holding))
		{
			// The central queue registration remains valid, so this requester
			// becomes Pending and can be promoted without losing its order.
		}
		NotifyRequesterSlotChanged(Requester);
	}
	RepackAllCorridorQueueLayers(true);
}

bool UCombatEngagementSlotComponent::ReconcilePocketAttackReservations()
{
	if (!IsPocketFormationActive())
	{
		return false;
	}

	// Validate the complete active row before touching live reservations. A transient
	// NavMesh projection failure must not make an attacking owner lose its slot.
	for (int32 SlotIndex = 0; SlotIndex < GetActiveAttackSlotCount(); ++SlotIndex)
	{
		FVector SlotLocation;
		if (!GetPocketSlotWorldLocation(
			EBHCombatSlotType::Attack,
			SlotIndex,
			SlotLocation))
		{
			return false;
		}
	}

	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<8>> LockedRequesters;
	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<8>> OtherRequesters;
	TSet<AActor*> SeenRequesters;
	const TArray<TWeakObjectPtr<AActor>> PreviousAttackReservations = AttackReservations;
	for (TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		AActor* Requester = Reservation.Get();
		if (Requester && !SeenRequesters.Contains(Requester))
		{
			SeenRequesters.Add(Requester);
			const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
			const bool bLocked = Enemy
				&& (Enemy->GetCombatState() == EBHEnemyCombatState::Attacking
					|| Enemy->GetCombatState() == EBHEnemyCombatState::Recovering);
			(bLocked ? LockedRequesters : OtherRequesters).Add(Requester);
		}
		Reservation.Reset();
	}

	auto AssignClosestFreeAttackSlot = [this](AActor* Requester)
	{
		if (!Requester)
		{
			return false;
		}

		int32 BestSlotIndex = INDEX_NONE;
		float BestDistanceSquared = TNumericLimits<float>::Max();
		for (int32 SlotIndex = 0; SlotIndex < GetActiveAttackSlotCount(); ++SlotIndex)
		{
			if (AttackReservations[SlotIndex].IsValid())
			{
				continue;
			}
			if (!CanRequesterOccupyAttackSlot(Requester, SlotIndex))
			{
				continue;
			}

			FVector SlotLocation;
			if (!GetPocketSlotWorldLocation(
				EBHCombatSlotType::Attack,
				SlotIndex,
				SlotLocation))
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared2D(
				Requester->GetActorLocation(),
				SlotLocation);
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				BestSlotIndex = SlotIndex;
			}
		}

		if (!AttackReservations.IsValidIndex(BestSlotIndex))
		{
			return false;
		}
		AttackReservations[BestSlotIndex] = Requester;
		return true;
	};

	for (const TWeakObjectPtr<AActor>& RequesterPtr : LockedRequesters)
	{
		if (!AssignClosestFreeAttackSlot(RequesterPtr.Get()))
		{
			AttackReservations = PreviousAttackReservations;
			return false;
		}
	}

	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<8>> OverflowRequesters;
	for (const TWeakObjectPtr<AActor>& RequesterPtr : OtherRequesters)
	{
		AActor* Requester = RequesterPtr.Get();
		if (Requester && !AssignClosestFreeAttackSlot(Requester))
		{
			OverflowRequesters.Add(Requester);
		}
	}

	for (const TWeakObjectPtr<AActor>& RequesterPtr : OverflowRequesters)
	{
		AActor* Requester = RequesterPtr.Get();
		if (!Requester)
		{
			continue;
		}
		const bool bDemotedToReservedLayer = TryReserveSlot(Requester, EBHCombatSlotType::Wait)
			|| TryReserveSlot(Requester, EBHCombatSlotType::Holding);
		if (!bDemotedToReservedLayer)
		{
			// The central queue remains valid, so this requester becomes Pending.
			NotifyRequesterSlotChanged(Requester);
		}
	}
	return true;
}

void UCombatEngagementSlotComponent::RepackCorridorLayerReservations(
	TArray<TWeakObjectPtr<AActor>>& Reservations,
	EBHCombatSlotType SlotType,
	bool bNotifyMovedRequesters)
{
	if (!IsCorridorFormationActive())
	{
		return;
	}

	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<16>> Requesters;
	TMap<AActor*, int32> PreviousSlotIndices;
	for (int32 SlotIndex = 0; SlotIndex < Reservations.Num(); ++SlotIndex)
	{
		TWeakObjectPtr<AActor>& Reservation = Reservations[SlotIndex];
		if (Reservation.IsValid())
		{
			Requesters.Add(Reservation);
			PreviousSlotIndices.Add(Reservation.Get(), SlotIndex);
		}
		Reservation.Reset();
	}
	const TArray<FCorridorRowSlot>* Layout = SlotType == EBHCombatSlotType::Wait
		? &CorridorWaitRowLayout
		: (SlotType == EBHCombatSlotType::Holding ? &CorridorHoldingRowLayout : nullptr);
	while (!Requesters.IsEmpty())
	{
		int32 BestRequesterIndex = INDEX_NONE;
		int32 BestSlotIndex = INDEX_NONE;
		float BestTravelSquared = TNumericLimits<float>::Max();
		uint64 BestSequence = TNumericLimits<uint64>::Max();
		for (int32 RequesterIndex = 0; RequesterIndex < Requesters.Num(); ++RequesterIndex)
		{
			AActor* Requester = Requesters[RequesterIndex].Get();
			if (!Requester)
			{
				continue;
			}

			const int32 RequesterSideIndex = GetCorridorSideIndex(Requester);
			const uint64 Sequence = GetQueueSequence(Requester);
			for (int32 SlotIndex = 0; SlotIndex < Reservations.Num(); ++SlotIndex)
			{
				if (Reservations[SlotIndex].IsValid()
					|| !Layout
					|| !Layout->IsValidIndex(SlotIndex)
					|| (*Layout)[SlotIndex].SideIndex != RequesterSideIndex)
				{
					continue;
				}

				const float TravelSquared = FVector::DistSquared2D(
					Requester->GetActorLocation(),
					(*Layout)[SlotIndex].WorldLocation);
				const bool bShorterTravel = TravelSquared + 1.0f < BestTravelSquared;
				const bool bSameTravelOlder = FMath::IsNearlyEqual(
					TravelSquared,
					BestTravelSquared,
					1.0f) && Sequence < BestSequence;
				if (BestRequesterIndex == INDEX_NONE || bShorterTravel || bSameTravelOlder)
				{
					BestRequesterIndex = RequesterIndex;
					BestSlotIndex = SlotIndex;
					BestTravelSquared = TravelSquared;
					BestSequence = Sequence;
				}
			}
		}

		if (BestRequesterIndex == INDEX_NONE || BestSlotIndex == INDEX_NONE)
		{
			break;
		}

		AActor* Requester = Requesters[BestRequesterIndex].Get();
		const int32* PreviousSlotIndex = PreviousSlotIndices.Find(Requester);
		Reservations[BestSlotIndex] = Requester;
		if (bNotifyMovedRequesters
			&& (!PreviousSlotIndex
				|| *PreviousSlotIndex != BestSlotIndex))
		{
			NotifyRequesterSlotChanged(Requester);
		}
		Requesters.RemoveAtSwap(BestRequesterIndex, 1, EAllowShrinking::No);
	}

	if (bNotifyMovedRequesters)
	{
		for (const TWeakObjectPtr<AActor>& UnassignedRequester : Requesters)
		{
			NotifyRequesterSlotChanged(UnassignedRequester.Get());
		}
	}

	UE_LOG(
		LogProjectBH,
		VeryVerbose,
		TEXT("%s repacked Corridor %s reservations by side-local minimum travel."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		*StaticEnum<EBHCombatSlotType>()->GetNameStringByValue(static_cast<int64>(SlotType)));
}

void UCombatEngagementSlotComponent::RepackAllCorridorQueueLayers(bool bNotifyMovedRequesters)
{
	if (!IsCorridorFormationActive())
	{
		return;
	}

	RepackCorridorLayerReservations(
		WaitReservations,
		EBHCombatSlotType::Wait,
		bNotifyMovedRequesters);
	RepackCorridorLayerReservations(
		HoldingReservations,
		EBHCombatSlotType::Holding,
		bNotifyMovedRequesters);
}

void UCombatEngagementSlotComponent::AssignCorridorSideIndices(bool bResetExistingAssignments)
{
	if (!IsCorridorFormationActive())
	{
		return;
	}

	const AActor* Owner = GetOwner();
	const FVector AxisDirection = CorridorFormationRearDirection.GetSafeNormal2D();
	if (!Owner || AxisDirection.IsNearlyZero())
	{
		return;
	}

	int32 SideCounts[2] = { 0, 0 };
	for (FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (bResetExistingAssignments)
		{
			Entry.CorridorSideIndex = INDEX_NONE;
		}
		else if (FMath::IsWithin(Entry.CorridorSideIndex, 0, 2))
		{
			++SideCounts[Entry.CorridorSideIndex];
		}
	}

	for (FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* Requester = Entry.Requester.Get();
		if (!Requester || FMath::IsWithin(Entry.CorridorSideIndex, 0, 2))
		{
			continue;
		}

		const float AxisOffset = FVector::DotProduct(
			Requester->GetActorLocation() - Owner->GetActorLocation(),
			AxisDirection);
		if (FMath::Abs(AxisOffset) > 25.0f)
		{
			Entry.CorridorSideIndex = AxisOffset >= 0.0f ? 0 : 1;
		}
		else
		{
			Entry.CorridorSideIndex = SideCounts[0] <= SideCounts[1] ? 0 : 1;
		}
		++SideCounts[Entry.CorridorSideIndex];
	}
}

int32 UCombatEngagementSlotComponent::GetCorridorLaneIndex(AActor* Requester) const
{
	if (!Requester)
	{
		return INDEX_NONE;
	}

	const int32 LaneCount = FMath::Max(1, ActiveCorridorLaneCount);
	const uint64 Sequence = GetQueueSequence(Requester);
	if (Sequence == TNumericLimits<uint64>::Max())
	{
		return static_cast<int32>(Requester->GetUniqueID() % static_cast<uint32>(LaneCount));
	}

	return static_cast<int32>((Sequence - 1) % static_cast<uint64>(LaneCount));
}

int32 UCombatEngagementSlotComponent::GetCorridorSideIndex(AActor* Requester) const
{
	if (!Requester)
	{
		return INDEX_NONE;
	}

	const AActor* Owner = GetOwner();
	const FVector AxisDirection = CorridorFormationRearDirection.GetSafeNormal2D();
	if (Owner && !AxisDirection.IsNearlyZero())
	{
		const float AxisOffset = FVector::DotProduct(
			Requester->GetActorLocation() - Owner->GetActorLocation(),
			AxisDirection);
		if (FMath::Abs(AxisOffset) >= FMath::Max(0.0f, CorridorPhysicalSideOverrideDistance))
		{
			// Actual position wins only after the requester has clearly crossed the
			// player axis. This lets a spawn or a genuine external bypass populate
			// the far side without allowing a slot assignment to cause the crossing.
			return AxisOffset >= 0.0f ? 0 : 1;
		}
	}

	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (Entry.Requester.Get() == Requester
			&& FMath::IsWithin(Entry.CorridorSideIndex, 0, 2))
		{
			return Entry.CorridorSideIndex;
		}
	}

	if (!Owner || AxisDirection.IsNearlyZero())
	{
		return static_cast<int32>(Requester->GetUniqueID() % 2u);
	}

	return FVector::DotProduct(
		Requester->GetActorLocation() - Owner->GetActorLocation(),
		AxisDirection) >= 0.0f ? 0 : 1;
}

bool UCombatEngagementSlotComponent::IsCorridorAttackSlotOnRequesterSide(
	AActor* Requester,
	int32 AttackSlotIndex) const
{
	return !IsCorridorFormationActive()
		|| (Requester
			&& GetCorridorSideIndex(Requester)
				== GetCorridorAttackSlotSideIndex(AttackSlotIndex));
}

int32 UCombatEngagementSlotComponent::GetCorridorQueueChannelIndex(AActor* Requester) const
{
	const int32 LaneIndex = GetCorridorLaneIndex(Requester);
	const int32 SideIndex = GetCorridorSideIndex(Requester);
	if (LaneIndex == INDEX_NONE || SideIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	return SideIndex * FMath::Max(1, ActiveCorridorLaneCount) + LaneIndex;
}

int32 UCombatEngagementSlotComponent::GetCorridorAttackSlotSideIndex(int32 AttackSlotIndex) const
{
	return ActiveCorridorAttackLayout.IsValidIndex(AttackSlotIndex)
		? ActiveCorridorAttackLayout[AttackSlotIndex].SideIndex
		: INDEX_NONE;
}

int32 UCombatEngagementSlotComponent::GetCorridorAttackSlotChannelIndex(int32 AttackSlotIndex) const
{
	if (!ActiveCorridorAttackLayout.IsValidIndex(AttackSlotIndex))
	{
		return INDEX_NONE;
	}

	const FCorridorAttackSlot& AttackSlot = ActiveCorridorAttackLayout[AttackSlotIndex];
	const int32 LaneCount = FMath::Max(1, ActiveCorridorLaneCount);
	return AttackSlot.SideIndex * LaneCount
		+ AttackSlot.SampleIndex % LaneCount;
}

void UCombatEngagementSlotComponent::UpdateCorridorSideForAttackReservation(
	AActor* Requester,
	int32 AttackSlotIndex)
{
	if (!Requester || !IsCorridorFormationActive())
	{
		return;
	}

	const int32 AttackSideIndex = GetCorridorAttackSlotSideIndex(AttackSlotIndex);
	if (!FMath::IsWithin(AttackSideIndex, 0, 2))
	{
		return;
	}

	for (FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (Entry.Requester.Get() == Requester)
		{
			Entry.CorridorSideIndex = AttackSideIndex;
			return;
		}
	}
}

bool UCombatEngagementSlotComponent::FindCorridorWaitAdmissionForAttackSlot(
	int32 AttackSlotIndex,
	int32& OutWaitSlotIndex,
	bool bRequireWaitArrival) const
{
	OutWaitSlotIndex = INDEX_NONE;
	if (!IsCorridorFormationActive()
		|| !AttackReservations.IsValidIndex(AttackSlotIndex)
		|| AttackSlotIndex >= GetActiveAttackSlotCount())
	{
		return false;
	}

	FVector AttackSlotLocation;
	if (!GetSlotWorldLocation(EBHCombatSlotType::Attack, AttackSlotIndex, AttackSlotLocation))
	{
		return false;
	}

	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	float BestPathScore = TNumericLimits<float>::Max();
	uint64 BestSequence = TNumericLimits<uint64>::Max();
	const int32 TargetSideIndex = GetCorridorAttackSlotSideIndex(AttackSlotIndex);
	for (int32 WaitSlotIndex = 0; WaitSlotIndex < WaitReservations.Num(); ++WaitSlotIndex)
	{
		AActor* Requester = WaitReservations[WaitSlotIndex].Get();
		if (!Requester
			|| !CorridorWaitRowLayout.IsValidIndex(WaitSlotIndex)
			|| CorridorWaitRowLayout[WaitSlotIndex].SideIndex != TargetSideIndex
			|| !IsCorridorAttackSlotOnRequesterSide(Requester, AttackSlotIndex)
			|| CurrentTime < GetAttackEligibleTime(Requester))
		{
			continue;
		}
		if (!CanRequesterOccupyAttackSlot(Requester, AttackSlotIndex))
		{
			continue;
		}

		FVector WaitSlotLocation;
		if (!GetSlotWorldLocation(
			EBHCombatSlotType::Wait,
			WaitSlotIndex,
			WaitSlotLocation)
			|| (bRequireWaitArrival
				&& FVector::DistSquared2D(Requester->GetActorLocation(), WaitSlotLocation)
					> FMath::Square(PromotionArrivalRadius)))
		{
			continue;
		}

		const float MaximumAttackSlotDistance = GetMaximumAttackSlotDistance(Requester);
		if (FVector::DistSquared2D(GetOwner()->GetActorLocation(), AttackSlotLocation)
			> FMath::Square(MaximumAttackSlotDistance))
		{
			continue;
		}

		float PathScore = 0.0f;
		if (!GetNavigationPathScore(Requester, AttackSlotLocation, PathScore))
		{
			continue;
		}

		const uint64 Sequence = GetQueueSequence(Requester);
		const bool bOlderRequester = Sequence < BestSequence;
		const bool bSameRequesterBetterPath = Sequence == BestSequence
			&& PathScore < BestPathScore;
		if (OutWaitSlotIndex == INDEX_NONE || bOlderRequester || bSameRequesterBetterPath)
		{
			OutWaitSlotIndex = WaitSlotIndex;
			BestSequence = Sequence;
			BestPathScore = PathScore;
		}
	}
	return OutWaitSlotIndex != INDEX_NONE;
}

int32 UCombatEngagementSlotComponent::GetLockedAttackReservationCount() const
{
	int32 LockedCount = 0;
	for (const TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		const ABHEnemy* Enemy = Cast<ABHEnemy>(Reservation.Get());
		if (Enemy
			&& (Enemy->GetCombatState() == EBHEnemyCombatState::Attacking
				|| Enemy->GetCombatState() == EBHEnemyCombatState::Recovering))
		{
			++LockedCount;
		}
	}
	return LockedCount;
}

FVector UCombatEngagementSlotComponent::ResolveCorridorRearDirection(const FVector& UnsignedAxis) const
{
	FVector Axis = UnsignedAxis.GetSafeNormal2D();
	const AActor* Owner = GetOwner();
	if (!Owner || Axis.IsNearlyZero())
	{
		return -FVector::ForwardVector;
	}

	float CrowdSideScore = 0.0f;
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		const AActor* Requester = Entry.Requester.Get();
		if (!Requester)
		{
			continue;
		}

		CrowdSideScore += FVector::DotProduct(
			Requester->GetActorLocation() - Owner->GetActorLocation(),
			Axis);
	}
	if (FMath::Abs(CrowdSideScore) > 25.0f)
	{
		return CrowdSideScore >= 0.0f ? Axis : -Axis;
	}

	const float AxisVelocity = FVector::DotProduct(Owner->GetVelocity(), Axis);
	if (FMath::Abs(AxisVelocity) > 10.0f)
	{
		return AxisVelocity >= 0.0f ? -Axis : Axis;
	}

	return FVector::DotProduct(Owner->GetActorForwardVector(), Axis) >= 0.0f
		? -Axis
		: Axis;
}

bool UCombatEngagementSlotComponent::IsCorridorFormationActive() const
{
	return bEnableCorridorFormation
		&& bHasValidSpaceAnalysis
		&& CurrentSpaceMode == EBHCombatSpaceMode::Corridor;
}

bool UCombatEngagementSlotComponent::IsPocketFormationActive() const
{
	return bHasValidSpaceAnalysis
		&& CurrentSpaceMode == EBHCombatSpaceMode::Pocket;
}

int32 UCombatEngagementSlotComponent::CalculateCorridorLaneCount(float CorridorWidth) const
{
	const float AgentRadius = FMath::Max(1.0f, CorridorAgentRadius);
	const float Spacing = FMath::Max(1.0f, CorridorSlotSpacing);
	const float UsableCenterWidth = FMath::Max(0.0f, CorridorWidth - 2.0f * AgentRadius);
	const int32 WidthLimitedLaneCount = 1 + FMath::FloorToInt(UsableCenterWidth / Spacing);
	return FMath::Clamp(WidthLimitedLaneCount, 1, FMath::Clamp(CorridorMaximumLaneCount, 1, 4));
}

bool UCombatEngagementSlotComponent::BuildCorridorAttackCandidateLayout(
	TArray<FCorridorAttackSlot>& OutLayout)
{
	OutLayout.Reset();
	CorridorAttackProbeLocations.Reset();
	CorridorAttackProbeValid.Reset();
	CorridorAttackProbeSelected.Reset();

	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	FVector ProjectedOrigin;
	const FVector AxisDirection = CorridorFormationRearDirection.GetSafeNormal2D();
	if (!Owner
		|| !NavigationSystem
		|| AxisDirection.IsNearlyZero()
		|| !ProjectToNavigation(Owner->GetActorLocation(), ProjectedOrigin))
	{
		return false;
	}

	const int32 SampleCount = FMath::Clamp(CorridorAttackCandidateCount, 8, 32);
	const float ProjectionToleranceSquared = FMath::Square(
		FMath::Max(0.0f, CorridorAttackProjectionTolerance));
	TArray<FCorridorAttackSlot> ValidCandidates;
	ValidCandidates.Reserve(SampleCount);
	CorridorAttackProbeLocations.SetNum(SampleCount);
	CorridorAttackProbeValid.Init(0, SampleCount);
	CorridorAttackProbeSelected.Init(0, SampleCount);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		FVector DesiredLocation;
		FVector CandidateDirection;
		if (!GetCorridorAttackCandidateDesiredLocation(
			SampleIndex,
			DesiredLocation,
			CandidateDirection))
		{
			continue;
		}
		CorridorAttackProbeLocations[SampleIndex] = DesiredLocation;

		FVector ProjectedLocation;
		FVector RaycastHitLocation;
		if (!ProjectToNavigation(DesiredLocation, ProjectedLocation)
			|| FVector::DistSquared2D(DesiredLocation, ProjectedLocation)
				> ProjectionToleranceSquared
			|| NavigationSystem->NavigationRaycast(
				World,
				ProjectedOrigin,
				ProjectedLocation,
				RaycastHitLocation)
			|| !HasCorridorAttackCandidateClearance(ProjectedLocation))
		{
			continue;
		}

		CorridorAttackProbeLocations[SampleIndex] = ProjectedLocation;
		CorridorAttackProbeValid[SampleIndex] = 1;
		FCorridorAttackSlot& Candidate = ValidCandidates.AddDefaulted_GetRef();
		Candidate.WorldLocation = ProjectedLocation;
		Candidate.SampleIndex = SampleIndex;
		const float AngleDegrees = 360.0f
			* static_cast<float>(SampleIndex) / static_cast<float>(SampleCount);
		Candidate.SideIndex = AngleDegrees < 90.0f || AngleDegrees >= 270.0f ? 0 : 1;
		Candidate.AxisAlignment = FMath::Abs(FVector::DotProduct(
			CandidateDirection,
			AxisDirection));
	}

	const int32 MaximumSelectedCount = FMath::Max(0, AttackReservations.Num());
	const float MinimumSpacingSquared = FMath::Square(
		FMath::Max(1.0f, CorridorAttackMinimumSpacing));
	TArray<int32> CurrentSelection;
	TArray<int32> BestSelection;
	float BestAxisAlignment = -1.0f;
	int32 BestSideImbalance = TNumericLimits<int32>::Max();
	int32 BestExistingOverlap = -1;
	auto IsActiveSample = [this](int32 SampleIndex)
	{
		for (const FCorridorAttackSlot& ActiveSlot : ActiveCorridorAttackLayout)
		{
			if (ActiveSlot.SampleIndex == SampleIndex)
			{
				return true;
			}
		}
		return false;
	};
	auto EvaluateSelection = [&]()
	{
		float AxisAlignment = 0.0f;
		int32 SideCounts[2] = { 0, 0 };
		int32 ExistingOverlap = 0;
		for (const int32 CandidateIndex : CurrentSelection)
		{
			const FCorridorAttackSlot& Candidate = ValidCandidates[CandidateIndex];
			AxisAlignment += Candidate.AxisAlignment;
			++SideCounts[Candidate.SideIndex];
			ExistingOverlap += IsActiveSample(Candidate.SampleIndex) ? 1 : 0;
		}
		const int32 SideImbalance = FMath::Abs(SideCounts[0] - SideCounts[1]);
		bool bBetter = CurrentSelection.Num() > BestSelection.Num();
		if (CurrentSelection.Num() == BestSelection.Num())
		{
			if (bMouthMixedActive)
			{
				bBetter = SideImbalance < BestSideImbalance
					|| (SideImbalance == BestSideImbalance
						&& AxisAlignment > BestAxisAlignment + UE_KINDA_SMALL_NUMBER)
					|| (SideImbalance == BestSideImbalance
						&& FMath::IsNearlyEqual(AxisAlignment, BestAxisAlignment)
						&& ExistingOverlap > BestExistingOverlap);
			}
			else
			{
				bBetter = AxisAlignment > BestAxisAlignment + UE_KINDA_SMALL_NUMBER
					|| (FMath::IsNearlyEqual(AxisAlignment, BestAxisAlignment)
						&& SideImbalance < BestSideImbalance)
					|| (FMath::IsNearlyEqual(AxisAlignment, BestAxisAlignment)
						&& SideImbalance == BestSideImbalance
						&& ExistingOverlap > BestExistingOverlap);
			}
		}
		if (bBetter)
		{
			BestSelection = CurrentSelection;
			BestAxisAlignment = AxisAlignment;
			BestSideImbalance = SideImbalance;
			BestExistingOverlap = ExistingOverlap;
		}
	};

	TFunction<void(int32)> SearchCandidates;
	SearchCandidates = [&](int32 StartIndex)
	{
		EvaluateSelection();
		if (CurrentSelection.Num() >= MaximumSelectedCount)
		{
			return;
		}
		if (CurrentSelection.Num() + ValidCandidates.Num() - StartIndex
			< BestSelection.Num())
		{
			return;
		}

		for (int32 CandidateIndex = StartIndex;
			CandidateIndex < ValidCandidates.Num();
			++CandidateIndex)
		{
			bool bHasSpacing = true;
			for (const int32 SelectedIndex : CurrentSelection)
			{
				if (FVector::DistSquared2D(
					ValidCandidates[CandidateIndex].WorldLocation,
					ValidCandidates[SelectedIndex].WorldLocation)
					< MinimumSpacingSquared)
				{
					bHasSpacing = false;
					break;
				}
			}
			if (!bHasSpacing)
			{
				continue;
			}

			CurrentSelection.Add(CandidateIndex);
			SearchCandidates(CandidateIndex + 1);
			CurrentSelection.Pop(EAllowShrinking::No);
		}
	};
	SearchCandidates(0);

	for (const int32 CandidateIndex : BestSelection)
	{
		const FCorridorAttackSlot& Candidate = ValidCandidates[CandidateIndex];
		OutLayout.Add(Candidate);
		if (CorridorAttackProbeSelected.IsValidIndex(Candidate.SampleIndex))
		{
			CorridorAttackProbeSelected[Candidate.SampleIndex] = 1;
		}
	}
	OutLayout.Sort([](const FCorridorAttackSlot& Left, const FCorridorAttackSlot& Right)
	{
		return Left.SampleIndex < Right.SampleIndex;
	});
	return true;
}

bool UCombatEngagementSlotComponent::GetCorridorAttackCandidateDesiredLocation(
	int32 SampleIndex,
	FVector& OutDesiredLocation,
	FVector& OutDirection) const
{
	const AActor* Owner = GetOwner();
	const FVector AxisDirection = CorridorFormationRearDirection.GetSafeNormal2D();
	const int32 SampleCount = FMath::Clamp(CorridorAttackCandidateCount, 8, 32);
	if (!Owner
		|| !FMath::IsWithin(SampleIndex, 0, SampleCount)
		|| AxisDirection.IsNearlyZero())
	{
		return false;
	}

	const float AngleDegrees = 360.0f
		* static_cast<float>(SampleIndex) / static_cast<float>(SampleCount);
	OutDirection = AxisDirection.RotateAngleAxis(AngleDegrees, FVector::UpVector).GetSafeNormal2D();
	OutDesiredLocation = Owner->GetActorLocation()
		+ OutDirection * FMath::Max(1.0f, AttackRingRadius);
	return !OutDirection.IsNearlyZero();
}

bool UCombatEngagementSlotComponent::HasCorridorAttackCandidateClearance(
	const FVector& CandidateLocation) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	if (!World || !NavigationSystem)
	{
		return false;
	}

	const float ClearanceRadius = FMath::Max(1.0f, CorridorAgentRadius);
	const int32 ProbeCount = FMath::Clamp(CorridorAttackClearanceProbeCount, 4, 16);
	for (int32 ProbeIndex = 0; ProbeIndex < ProbeCount; ++ProbeIndex)
	{
		const float AngleRadians = 2.0f * UE_PI
			* static_cast<float>(ProbeIndex) / static_cast<float>(ProbeCount);
		const FVector ProbeDirection(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0f);
		FVector RaycastHitLocation;
		if (NavigationSystem->NavigationRaycast(
			World,
			CandidateLocation,
			CandidateLocation + ProbeDirection * ClearanceRadius,
			RaycastHitLocation))
		{
			return false;
		}
	}
	return true;
}

bool UCombatEngagementSlotComponent::AreCorridorAttackLayoutsEquivalent(
	const TArray<FCorridorAttackSlot>& LayoutA,
	const TArray<FCorridorAttackSlot>& LayoutB) const
{
	if (LayoutA.Num() != LayoutB.Num())
	{
		return false;
	}
	for (int32 SlotIndex = 0; SlotIndex < LayoutA.Num(); ++SlotIndex)
	{
		if (LayoutA[SlotIndex].SampleIndex != LayoutB[SlotIndex].SampleIndex)
		{
			return false;
		}
	}
	return true;
}

void UCombatEngagementSlotComponent::RefreshCorridorAttackLayoutWorldLocations(
	TArray<FCorridorAttackSlot>& Layout) const
{
	const float ProjectionToleranceSquared = FMath::Square(
		FMath::Max(0.0f, CorridorAttackProjectionTolerance));
	for (FCorridorAttackSlot& Slot : Layout)
	{
		FVector DesiredLocation;
		FVector CandidateDirection;
		FVector ProjectedLocation;
		if (GetCorridorAttackCandidateDesiredLocation(
			Slot.SampleIndex,
			DesiredLocation,
			CandidateDirection)
			&& ProjectToNavigation(DesiredLocation, ProjectedLocation)
			&& FVector::DistSquared2D(DesiredLocation, ProjectedLocation)
				<= ProjectionToleranceSquared)
		{
			Slot.WorldLocation = ProjectedLocation;
		}
	}
}

void UCombatEngagementSlotComponent::CommitCorridorAttackLayout(
	TArray<FCorridorAttackSlot>&& NewLayout)
{
	ActiveCorridorAttackLayout = MoveTemp(NewLayout);
	DesiredCorridorAttackSlotCount = ActiveCorridorAttackLayout.Num();
	RefreshCorridorAttackSideCounts();
	DesiredCorridorSide0AttackSlotCount = ActiveCorridorSide0AttackSlotCount;
	DesiredCorridorSide1AttackSlotCount = ActiveCorridorSide1AttackSlotCount;
}

void UCombatEngagementSlotComponent::RefreshCorridorAttackSideCounts()
{
	ActiveCorridorAttackSlotCount = ActiveCorridorAttackLayout.Num();
	ActiveCorridorSide0AttackSlotCount = 0;
	ActiveCorridorSide1AttackSlotCount = 0;
	for (const FCorridorAttackSlot& Slot : ActiveCorridorAttackLayout)
	{
		(Slot.SideIndex == 0
			? ActiveCorridorSide0AttackSlotCount
			: ActiveCorridorSide1AttackSlotCount)++;
	}
}

int32 UCombatEngagementSlotComponent::CalculatePocketAttackSlotCount() const
{
	const int32 MaximumAttackCount = FMath::Max(1, AttackReservations.Num());
	const float Radius = FMath::Max(1.0f, AttackRingRadius);
	const float ArcHalfAngle = FMath::Clamp(
		FMath::Min(PocketMaximumArcHalfAngle, 0.5f * EstimatedPocketOpenArc),
		10.0f,
		170.0f);
	const float Spacing = FMath::Min(FMath::Max(1.0f, PocketSlotSpacing), 2.0f * Radius);
	const float StepAngle = FMath::RadiansToDegrees(2.0f * FMath::Asin(FMath::Clamp(
		Spacing / (2.0f * Radius),
		0.0f,
		1.0f)));
	const int32 ArcCapacity = 1 + FMath::FloorToInt(
		(2.0f * ArcHalfAngle) / FMath::Max(1.0f, StepAngle));
	return FMath::Clamp(ArcCapacity, 1, MaximumAttackCount);
}

bool UCombatEngagementSlotComponent::GetCorridorSlotWorldLocation(
	EBHCombatSlotType SlotType,
	int32 SlotIndex,
	FVector& OutWorldLocation) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || SlotIndex < 0 || !IsCorridorFormationActive())
	{
		return false;
	}

	const FVector AxisDirection = CorridorFormationRearDirection.GetSafeNormal2D();
	if (AxisDirection.IsNearlyZero())
	{
		return false;
	}

	if (SlotType == EBHCombatSlotType::Attack)
	{
		const int32 ActiveAttackCount = GetActiveAttackSlotCount();
		if (SlotIndex >= ActiveAttackCount
			|| !ActiveCorridorAttackLayout.IsValidIndex(SlotIndex))
		{
			return false;
		}

		OutWorldLocation = ActiveCorridorAttackLayout[SlotIndex].WorldLocation;
		return true;
	}

	return GetCorridorDynamicSlotWorldLocation(SlotType, SlotIndex, OutWorldLocation);
}

bool UCombatEngagementSlotComponent::GetCorridorPendingWorldLocation(
	int32 PendingIndex,
	FVector& OutWorldLocation) const
{
	if (PendingIndex < 0)
	{
		return false;
	}

	return GetCorridorSlotWorldLocation(
		EBHCombatSlotType::Pending,
		PendingIndex,
		OutWorldLocation);
}

bool UCombatEngagementSlotComponent::GetCorridorPendingWorldLocationForRequester(
	AActor* Requester,
	int32& OutPendingIndex,
	FVector& OutWorldLocation) const
{
	OutPendingIndex = INDEX_NONE;
	OutWorldLocation = FVector::ZeroVector;
	if (!Requester || !IsCorridorFormationActive())
	{
		return false;
	}

	const uint64 RequesterSequence = GetQueueSequence(Requester);
	const int32 RequesterSideIndex = GetCorridorSideIndex(Requester);
	if (RequesterSequence == TNumericLimits<uint64>::Max()
		|| !FMath::IsWithin(RequesterSideIndex, 0, 2))
	{
		return false;
	}

	int32 SideLocalPendingRank = 0;
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* OtherRequester = Entry.Requester.Get();
		if (!OtherRequester
			|| IsRequesterReserved(OtherRequester)
			|| Entry.Sequence >= RequesterSequence
			|| GetCorridorSideIndex(OtherRequester) != RequesterSideIndex)
		{
			continue;
		}
		++SideLocalPendingRank;
	}

	TArray<FCorridorRowSlot> PendingLayout;
	float IgnoredLastRowDistance = CorridorPendingRowStartDistance;
	const int32 RequiredSlotCount = FMath::Max(2, (SideLocalPendingRank + 1) * 2);
	if (!BuildCorridorLayerLayout(
		EBHCombatSlotType::Pending,
		CorridorPendingRowStartDistance,
		RequiredSlotCount,
		PendingLayout,
		IgnoredLastRowDistance))
	{
		return false;
	}

	int32 MatchingSideRank = 0;
	for (int32 LayoutIndex = 0; LayoutIndex < PendingLayout.Num(); ++LayoutIndex)
	{
		const FCorridorRowSlot& Slot = PendingLayout[LayoutIndex];
		if (Slot.SideIndex != RequesterSideIndex)
		{
			continue;
		}
		if (MatchingSideRank++ == SideLocalPendingRank)
		{
			OutPendingIndex = LayoutIndex;
			OutWorldLocation = Slot.WorldLocation;
			return true;
		}
	}

	return false;
}

bool UCombatEngagementSlotComponent::GetPocketSlotWorldLocation(
	EBHCombatSlotType SlotType,
	int32 SlotIndex,
	FVector& OutWorldLocation) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || SlotIndex < 0 || !IsPocketFormationActive())
	{
		return false;
	}

	int32 SlotCount = 0;
	float BaseRadius = 0.0f;
	FVector Center = EngagementAnchorLocation;
	switch (SlotType)
	{
	case EBHCombatSlotType::Attack:
		SlotCount = GetActiveAttackSlotCount();
		BaseRadius = AttackRingRadius;
		Center = Owner->GetActorLocation();
		break;
	case EBHCombatSlotType::Wait:
		SlotCount = WaitReservations.Num();
		BaseRadius = WaitRingRadius;
		break;
	case EBHCombatSlotType::Holding:
		SlotCount = HoldingReservations.Num();
		BaseRadius = HoldingRingRadius;
		break;
	default:
		return false;
	}

	return SlotIndex < SlotCount
		&& GetPocketFanWorldLocation(
			Center,
			BaseRadius,
			SlotIndex,
			SlotCount,
			SlotType != EBHCombatSlotType::Attack,
			OutWorldLocation);
}

bool UCombatEngagementSlotComponent::GetPocketPendingWorldLocation(
	int32 PendingIndex,
	FVector& OutWorldLocation) const
{
	if (PendingIndex < 0 || !IsPocketFormationActive())
	{
		return false;
	}

	const int32 StablePendingCount = FMath::Max(
		FMath::Max(1, PendingSlotsPerRing),
		PendingIndex + 1);
	return GetPocketFanWorldLocation(
		EngagementAnchorLocation,
		PendingRingRadius,
		PendingIndex,
		StablePendingCount,
		true,
		OutWorldLocation);
}

bool UCombatEngagementSlotComponent::GetPocketFanWorldLocation(
	const FVector& Center,
	float BaseRadius,
	int32 SlotIndex,
	int32 SlotCount,
	bool bAllowMultipleRows,
	FVector& OutWorldLocation) const
{
	if (SlotIndex < 0 || SlotCount <= 0 || SlotIndex >= SlotCount)
	{
		return false;
	}

	FVector OpenDirection = PocketFormationDirection.GetSafeNormal2D();
	if (OpenDirection.IsNearlyZero())
	{
		OpenDirection = EstimatedPocketOpenDirection.GetSafeNormal2D();
	}
	if (OpenDirection.IsNearlyZero())
	{
		return false;
	}

	const float Radius = FMath::Max(1.0f, BaseRadius);
	const float ArcHalfAngle = FMath::Clamp(
		FMath::Min(PocketMaximumArcHalfAngle, 0.5f * EstimatedPocketOpenArc),
		10.0f,
		170.0f);
	const float Spacing = FMath::Min(FMath::Max(1.0f, PocketSlotSpacing), 2.0f * Radius);
	const float BaseStepAngle = FMath::RadiansToDegrees(2.0f * FMath::Asin(FMath::Clamp(
		Spacing / (2.0f * Radius),
		0.0f,
		1.0f)));
	const int32 SlotsPerRow = bAllowMultipleRows
		? FMath::Max(
			1,
			FMath::Min(
				SlotCount,
				1 + FMath::FloorToInt((2.0f * ArcHalfAngle) / FMath::Max(1.0f, BaseStepAngle))))
		: SlotCount;
	const int32 RowIndex = SlotIndex / SlotsPerRow;
	const int32 SlotIndexOnRow = SlotIndex % SlotsPerRow;
	const int32 RowStartIndex = RowIndex * SlotsPerRow;
	const int32 SlotsOnRow = FMath::Min(SlotsPerRow, SlotCount - RowStartIndex);
	const float RowRadius = Radius + static_cast<float>(RowIndex) * FMath::Max(1.0f, PocketRowSpacing);
	const float RowSpacing = FMath::Min(FMath::Max(1.0f, PocketSlotSpacing), 2.0f * RowRadius);
	const float RowStepAngle = FMath::RadiansToDegrees(2.0f * FMath::Asin(FMath::Clamp(
		RowSpacing / (2.0f * RowRadius),
		0.0f,
		1.0f)));
	const float CenteredIndex = static_cast<float>(SlotIndexOnRow)
		- 0.5f * static_cast<float>(SlotsOnRow - 1);
	const float FittedStepAngle = SlotsOnRow > 1
		? FMath::Min(RowStepAngle, (2.0f * ArcHalfAngle) / static_cast<float>(SlotsOnRow - 1))
		: 0.0f;
	const float DesiredAngle = FMath::Clamp(
		CenteredIndex * FittedStepAngle,
		-ArcHalfAngle,
		ArcHalfAngle);

	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	for (int32 AttemptIndex = 0; AttemptIndex < 7; ++AttemptIndex)
	{
		const float AngleScale = 1.0f - static_cast<float>(AttemptIndex) / 7.0f;
		const FVector SlotDirection = OpenDirection.RotateAngleAxis(
			DesiredAngle * AngleScale,
			FVector::UpVector);
		const FVector DesiredLocation = Center + SlotDirection * RowRadius;
		FVector HitLocation = DesiredLocation;
		const bool bPathBlocked = NavigationSystem
			&& NavigationSystem->NavigationRaycast(World, Center, DesiredLocation, HitLocation);
		if (!bPathBlocked && ProjectToNavigation(DesiredLocation, OutWorldLocation))
		{
			return true;
		}
	}

	return ProjectToNavigation(Center + OpenDirection * RowRadius, OutWorldLocation);
}

bool UCombatEngagementSlotComponent::TryReserveAttackSlot(
	AActor* Requester,
	int32 ExcludedSlotIndex)
{
	if (!Requester || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	PruneInvalidReservations();
	const float MaxDistanceFromOwner = GetMaximumAttackSlotDistance(Requester);

	int32 ExistingAttackIndex = INDEX_NONE;
	if (FindReservation(AttackReservations, Requester, ExistingAttackIndex))
	{
		FVector ExistingSlotLocation;
		const bool bHasValidLocation = GetSlotWorldLocation(
			EBHCombatSlotType::Attack,
			ExistingAttackIndex,
			ExistingSlotLocation);
		const bool bWithinAttackRange = bHasValidLocation
			&& FVector::DistSquared2D(GetOwner()->GetActorLocation(), ExistingSlotLocation)
				<= FMath::Square(MaxDistanceFromOwner);
		if (bWithinAttackRange)
		{
			return true;
		}

		AttackReservations[ExistingAttackIndex].Reset();
	}

	if (!TryReserveSlot(Requester, EBHCombatSlotType::Attack, MaxDistanceFromOwner, ExcludedSlotIndex))
	{
		return false;
	}

	for (TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
		}
	}
	for (TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
		}
	}

	return true;
}

bool UCombatEngagementSlotComponent::TryReserveWaitSlot(AActor* Requester, int32 ExcludedSlotIndex)
{
	if (!Requester || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	PruneInvalidReservations();

	int32 ExistingAttackIndex = INDEX_NONE;
	if (FindReservation(AttackReservations, Requester, ExistingAttackIndex))
	{
		return false;
	}

	int32 ExistingWaitIndex = INDEX_NONE;
	if (FindReservation(WaitReservations, Requester, ExistingWaitIndex))
	{
		return true;
	}

	if (!TryReserveSlot(Requester, EBHCombatSlotType::Wait, -1.0f, ExcludedSlotIndex))
	{
		return false;
	}

	for (TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
		}
	}
	return true;
}

bool UCombatEngagementSlotComponent::RequestEngagementSlot(
	AActor* Requester,
	EBHCombatSlotType ExcludedSlotType,
	int32 ExcludedSlotIndex)
{
	if (!Requester || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	PruneInvalidReservations();
	RegisterQueueRequester(Requester);
	if (IsRequesterReserved(Requester))
	{
		return true;
	}

	const int32 ExcludedAttackIndex = ExcludedSlotType == EBHCombatSlotType::Attack
		? ExcludedSlotIndex
		: INDEX_NONE;
	const int32 ExcludedWaitIndex = ExcludedSlotType == EBHCombatSlotType::Wait
		? ExcludedSlotIndex
		: INDEX_NONE;
	const int32 ExcludedHoldingIndex = ExcludedSlotType == EBHCombatSlotType::Holding
		? ExcludedSlotIndex
		: INDEX_NONE;

	// Initial assignments are provisional. The central manager selects Attack
	// owners by navigation path plus live congestion cost once registration settles.
	if (IsInitialFormationActive())
	{
		if (TryReserveWaitSlot(Requester, ExcludedWaitIndex))
		{
			return true;
		}

		return TryReserveSlot(
			Requester,
			EBHCombatSlotType::Holding,
			-1.0f,
			ExcludedHoldingIndex);
	}

	// Runtime arrivals cannot overtake actors already waiting in an outer layer.
	if (!HasAnyValidReservation(WaitReservations)
		&& !HasAnyValidReservation(HoldingReservations)
		&& TryReserveAttackSlot(Requester, ExcludedAttackIndex))
	{
		return true;
	}

	if (!HasAnyValidReservation(HoldingReservations)
		&& TryReserveWaitSlot(Requester, ExcludedWaitIndex))
	{
		return true;
	}

	return TryReserveSlot(
		Requester,
		EBHCombatSlotType::Holding,
		-1.0f,
		ExcludedHoldingIndex);
}

bool UCombatEngagementSlotComponent::GetReservedSlot(
	AActor* Requester,
	EBHCombatSlotType& OutSlotType,
	int32& OutSlotIndex,
	FVector& OutWorldLocation) const
{
	OutSlotType = EBHCombatSlotType::None;
	OutSlotIndex = INDEX_NONE;
	OutWorldLocation = FVector::ZeroVector;

	if (!Requester)
	{
		return false;
	}

	if (FindReservation(AttackReservations, Requester, OutSlotIndex))
	{
		OutSlotType = EBHCombatSlotType::Attack;
		return GetSlotWorldLocation(OutSlotType, OutSlotIndex, OutWorldLocation);
	}

	if (FindReservation(WaitReservations, Requester, OutSlotIndex))
	{
		OutSlotType = EBHCombatSlotType::Wait;
		return GetSlotWorldLocation(OutSlotType, OutSlotIndex, OutWorldLocation);
	}

	if (FindReservation(HoldingReservations, Requester, OutSlotIndex))
	{
		OutSlotType = EBHCombatSlotType::Holding;
		return GetSlotWorldLocation(OutSlotType, OutSlotIndex, OutWorldLocation);
	}

	return false;
}

bool UCombatEngagementSlotComponent::GetPendingWaitLocation(
	AActor* Requester,
	int32& OutPendingIndex,
	FVector& OutWorldLocation) const
{
	OutPendingIndex = INDEX_NONE;
	OutWorldLocation = FVector::ZeroVector;
	const AActor* Owner = GetOwner();
	if (!Requester || !Owner || IsRequesterReserved(Requester)
		|| !FindPendingRequesterIndex(Requester, OutPendingIndex))
	{
		return false;
	}
	if (IsCorridorFormationActive())
	{
		return GetCorridorPendingWorldLocationForRequester(
			Requester,
			OutPendingIndex,
			OutWorldLocation);
	}
	if (IsPocketFormationActive())
	{
		return GetPocketPendingWorldLocation(OutPendingIndex, OutWorldLocation);
	}

	const int32 SlotsPerRing = FMath::Max(1, PendingSlotsPerRing);
	const int32 RingIndex = OutPendingIndex / SlotsPerRing;
	const int32 SlotIndexOnRing = OutPendingIndex % SlotsPerRing;
	const float RingRadius = FMath::Max(0.0f, PendingRingRadius)
		+ static_cast<float>(RingIndex) * FMath::Max(0.0f, PendingRingSpacing);
	const float AngleDegrees = PendingRingAngleOffset
		+ 360.0f * static_cast<float>(SlotIndexOnRing) / static_cast<float>(SlotsPerRing);
	const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
	const FVector RingDirection(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0f);
	return ProjectToNavigation(EngagementAnchorLocation + RingDirection * RingRadius, OutWorldLocation);
}

bool UCombatEngagementSlotComponent::GetMoveGoalForReservedSlot(
	AActor* Requester,
	EBHCombatSlotType SlotType,
	int32 SlotIndex,
	const FVector& FinalSlotLocation,
	EBHCombatMoveRouteStage PreviousRouteStage,
	FVector& OutMoveGoal,
	EBHCombatMoveRouteStage& OutRouteStage) const
{
	OutMoveGoal = FinalSlotLocation;
	OutRouteStage = EBHCombatMoveRouteStage::Direct;

	const AActor* Owner = GetOwner();
	if (!Requester || !Owner || SlotType == EBHCombatSlotType::None || SlotIndex == INDEX_NONE)
	{
		return false;
	}
	const FVector RequesterLocation = Requester->GetActorLocation();
	if (SlotType == EBHCombatSlotType::Attack
		&& CanUseDirectAttackCoreExit(Requester, FinalSlotLocation))
	{
		// A core intruder promoted into the vacated radial Attack slot already has
		// a clear outward destination. Sending it to the full escape ring first
		// would overshoot the handed-over slot and recreate the blockage.
		return true;
	}
	if (ShouldUseCombatCoreEscape(Requester, PreviousRouteStage))
	{
		return ResolveCombatCoreEscapeGoal(
			Requester,
			OutMoveGoal,
			OutRouteStage);
	}
	if (IsCorridorFormationActive())
	{
		FVector RequesterDirection = RequesterLocation - Owner->GetActorLocation();
		RequesterDirection.Z = 0.0f;
		FVector TargetDirection = FinalSlotLocation - Owner->GetActorLocation();
		TargetDirection.Z = 0.0f;
		const float RouteAngleDifference = FMath::Abs(FMath::FindDeltaAngleDegrees(
			FMath::RadiansToDegrees(FMath::Atan2(RequesterDirection.Y, RequesterDirection.X)),
			FMath::RadiansToDegrees(FMath::Atan2(TargetDirection.Y, TargetDirection.X))));
		const bool bContinuesCoreBypass = PreviousRouteStage == EBHCombatMoveRouteStage::ApproachRing
			|| PreviousRouteStage == EBHCombatMoveRouteStage::BypassCorePositive
			|| PreviousRouteStage == EBHCombatMoveRouteStage::BypassCoreNegative;
		const bool bMayExitCoreBypass = !bContinuesCoreBypass
			|| RouteAngleDifference <= RingIngressAngleTolerance;
		float DirectPathScore = 0.0f;
		if (bMayExitCoreBypass
			&& !DoesSegmentCrossCombatCore(RequesterLocation, FinalSlotLocation)
			&& GetNavigationPathScoreBetween(
				Requester,
				RequesterLocation,
				FinalSlotLocation,
				DirectPathScore,
				true))
		{
			return true;
		}
		return ResolveCorridorCombatCoreBypassGoal(
			Requester,
			FinalSlotLocation,
			PreviousRouteStage,
			OutMoveGoal,
			OutRouteStage);
	}
	if (IsPocketFormationActive())
	{
		// Pocket slots already face their valid open arc. Full-ring orbiting would
		// push actors back into the blocked side of the pocket.
		return true;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const FVector RouteCenter = SlotType == EBHCombatSlotType::Attack
		? OwnerLocation
		: EngagementAnchorLocation;
	FVector RequesterDirection = RequesterLocation - RouteCenter;
	RequesterDirection.Z = 0.0f;
	FVector TargetDirection = FinalSlotLocation - RouteCenter;
	TargetDirection.Z = 0.0f;
	const float FinalSlotRadius = TargetDirection.Size2D();
	if (FinalSlotRadius <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	TargetDirection /= FinalSlotRadius;
	const float RequesterRadius = RequesterDirection.Size2D();
	if (RequesterRadius <= UE_KINDA_SMALL_NUMBER)
	{
		RequesterDirection = -TargetDirection;
	}
	else
	{
		RequesterDirection /= RequesterRadius;
	}
	const float RequesterAngle = FMath::RadiansToDegrees(FMath::Atan2(RequesterDirection.Y, RequesterDirection.X));
	const float TargetAngle = FMath::RadiansToDegrees(FMath::Atan2(TargetDirection.Y, TargetDirection.X));
	const float DeltaAngle = FMath::FindDeltaAngleDegrees(RequesterAngle, TargetAngle);
	const bool bDirectPathCrossesCombatCore = DoesSegmentCrossCombatCore(RequesterLocation, FinalSlotLocation);

	float FinalRingRadius = 0.0f;
	float IngressStagingRadius = 0.0f;
	if (SlotType == EBHCombatSlotType::Attack)
	{
		FinalRingRadius = FinalSlotRadius;
		IngressStagingRadius = WaitRingRadius;
	}
	else if (SlotType == EBHCombatSlotType::Wait)
	{
		FinalRingRadius = FinalSlotRadius;
		IngressStagingRadius = HoldingRingRadius;
	}

	const bool bUsesInnerRingIngress = IngressStagingRadius > FinalRingRadius;
	const bool bOutsideFinalRing = bUsesInnerRingIngress
		&& RequesterRadius > FinalRingRadius + OrbitRingAcceptanceRadius;

	// Once inward ingress begins, never send the actor back to the staging ring
	// for the same reservation. This prevents small owner movements from making
	// the route oscillate between alignment and ingress.
	if (PreviousRouteStage == EBHCombatMoveRouteStage::Ingress
		&& bOutsideFinalRing
		&& !bDirectPathCrossesCombatCore)
	{
		OutRouteStage = EBHCombatMoveRouteStage::Ingress;
		return true;
	}

	if (bOutsideFinalRing)
	{
		if (FMath::Abs(DeltaAngle) <= RingIngressAngleTolerance
			&& !bDirectPathCrossesCombatCore)
		{
			OutRouteStage = EBHCombatMoveRouteStage::Ingress;
			return true;
		}

		FVector DesiredWaypoint;
		const bool bContinueRingAlignment = PreviousRouteStage == EBHCombatMoveRouteStage::AlignOnRing;
		const bool bAtIngressStagingRing = FMath::Abs(RequesterRadius - IngressStagingRadius)
			<= OrbitRingAcceptanceRadius;
		if (!bContinueRingAlignment && !bAtIngressStagingRing)
		{
			DesiredWaypoint = RouteCenter + RequesterDirection * IngressStagingRadius;
			OutRouteStage = EBHCombatMoveRouteStage::ApproachRing;
		}
		else
		{
			const float StepAngle = FMath::Clamp(
				DeltaAngle,
				-OrbitWaypointAngleStep,
				OrbitWaypointAngleStep);
			const float NextAngleRadians = FMath::DegreesToRadians(RequesterAngle + StepAngle);
			DesiredWaypoint = RouteCenter
				+ FVector(FMath::Cos(NextAngleRadians), FMath::Sin(NextAngleRadians), 0.0f)
					* IngressStagingRadius;
			OutRouteStage = EBHCombatMoveRouteStage::AlignOnRing;
		}

		if (!ProjectToNavigation(DesiredWaypoint, OutMoveGoal))
		{
			return false;
		}

		return true;
	}

	if (!bDirectPathCrossesCombatCore)
	{
		return true;
	}

	float RouteRingRadius = WaitRingRadius;
	if (SlotType == EBHCombatSlotType::Holding)
	{
		RouteRingRadius = HoldingRingRadius;
	}
	else if (SlotType == EBHCombatSlotType::Pending)
	{
		RouteRingRadius = FinalSlotRadius;
	}
	const float OrbitRadius = FMath::Max3(
		RouteRingRadius,
		AttackRingRadius + OrbitRingAcceptanceRadius,
		GetEffectiveCombatCoreRadius() + OrbitRingAcceptanceRadius);
	FVector DesiredWaypoint;
	if (FMath::Abs(RequesterRadius - OrbitRadius) > OrbitRingAcceptanceRadius)
	{
		DesiredWaypoint = RouteCenter + RequesterDirection * OrbitRadius;
	}
	else
	{
		const float StepAngle = FMath::Clamp(
			DeltaAngle,
			-OrbitWaypointAngleStep,
			OrbitWaypointAngleStep);
		const float NextAngleRadians = FMath::DegreesToRadians(RequesterAngle + StepAngle);
		DesiredWaypoint = RouteCenter
			+ FVector(FMath::Cos(NextAngleRadians), FMath::Sin(NextAngleRadians), 0.0f) * OrbitRadius;
	}

	if (!ProjectToNavigation(DesiredWaypoint, OutMoveGoal))
	{
		return false;
	}

	OutRouteStage = FMath::Abs(RequesterRadius - OrbitRadius) > OrbitRingAcceptanceRadius
		? EBHCombatMoveRouteStage::ApproachRing
		: EBHCombatMoveRouteStage::AlignOnRing;
	return true;
}

bool UCombatEngagementSlotComponent::ShouldDeferWaitIngress(
	AActor* WaitRequester,
	const FVector& WaitMoveGoal,
	AActor*& OutBlockingAttackRequester) const
{
	OutBlockingAttackRequester = nullptr;
	UWorld* World = GetWorld();
	if (!bEnableAttackIngressPriority || !WaitRequester || !World)
	{
		return false;
	}

	UNavigationPath* WaitPath = UNavigationSystemV1::FindPathToLocationSynchronously(
		World,
		WaitRequester->GetActorLocation(),
		WaitMoveGoal,
		WaitRequester);
	if (!WaitPath || !WaitPath->IsValid() || WaitPath->IsPartial()
		|| WaitPath->PathPoints.Num() < 2)
	{
		return false;
	}

	const ABHEnemy* WaitEnemy = Cast<ABHEnemy>(WaitRequester);
	const UCapsuleComponent* WaitCapsule = WaitEnemy
		? WaitEnemy->GetCapsuleComponent()
		: nullptr;
	const float WaitRadius = WaitCapsule
		? WaitCapsule->GetScaledCapsuleRadius()
		: 45.0f;
	const float HeightTolerance = FMath::Max(0.0f, AttackIngressTrafficHeightTolerance);
	for (int32 AttackSlotIndex = 0;
		AttackSlotIndex < GetActiveAttackSlotCount();
		++AttackSlotIndex)
	{
		AActor* AttackRequester = AttackReservations.IsValidIndex(AttackSlotIndex)
			? AttackReservations[AttackSlotIndex].Get()
			: nullptr;
		const ABHEnemy* AttackEnemy = Cast<ABHEnemy>(AttackRequester);
		if (!AttackEnemy
			|| AttackRequester == WaitRequester
			|| AttackEnemy->GetCombatState() != EBHEnemyCombatState::Chasing)
		{
			continue;
		}

		FVector AttackSlotLocation;
		if (!GetSlotWorldLocation(
			EBHCombatSlotType::Attack,
			AttackSlotIndex,
			AttackSlotLocation)
			|| FVector::DistSquared2D(
				AttackRequester->GetActorLocation(),
				AttackSlotLocation)
				<= FMath::Square(FMath::Max(0.0f, AttackIngressTrafficArrivalRadius)))
		{
			continue;
		}

		UNavigationPath* AttackPath = UNavigationSystemV1::FindPathToLocationSynchronously(
			World,
			AttackRequester->GetActorLocation(),
			AttackSlotLocation,
			AttackRequester);
		if (!AttackPath || !AttackPath->IsValid() || AttackPath->IsPartial()
			|| AttackPath->PathPoints.Num() < 2)
		{
			continue;
		}

		const UCapsuleComponent* AttackCapsule = AttackEnemy->GetCapsuleComponent();
		const float AttackRadius = AttackCapsule
			? AttackCapsule->GetScaledCapsuleRadius()
			: 45.0f;
		const float ConflictRadius = WaitRadius
			+ AttackRadius
			+ FMath::Max(0.0f, AttackIngressPathPadding);
		const float ConflictRadiusSquared = FMath::Square(ConflictRadius);
		for (int32 WaitPointIndex = 1;
			WaitPointIndex < WaitPath->PathPoints.Num();
			++WaitPointIndex)
		{
			const FVector WaitStart = WaitPath->PathPoints[WaitPointIndex - 1];
			const FVector WaitEnd = WaitPath->PathPoints[WaitPointIndex];
			const float WaitMidZ = 0.5f * (WaitStart.Z + WaitEnd.Z);
			for (int32 AttackPointIndex = 1;
				AttackPointIndex < AttackPath->PathPoints.Num();
				++AttackPointIndex)
			{
				const FVector AttackStart = AttackPath->PathPoints[AttackPointIndex - 1];
				const FVector AttackEnd = AttackPath->PathPoints[AttackPointIndex];
				const float AttackMidZ = 0.5f * (AttackStart.Z + AttackEnd.Z);
				if (FMath::Abs(WaitMidZ - AttackMidZ) > HeightTolerance)
				{
					continue;
				}

				FVector ClosestOnWait;
				FVector ClosestOnAttack;
				FMath::SegmentDistToSegmentSafe(
					FVector(WaitStart.X, WaitStart.Y, 0.0f),
					FVector(WaitEnd.X, WaitEnd.Y, 0.0f),
					FVector(AttackStart.X, AttackStart.Y, 0.0f),
					FVector(AttackEnd.X, AttackEnd.Y, 0.0f),
					ClosestOnWait,
					ClosestOnAttack);
				if (FVector::DistSquared2D(ClosestOnWait, ClosestOnAttack)
					<= ConflictRadiusSquared)
				{
					OutBlockingAttackRequester = AttackRequester;
					return true;
				}
			}
		}
	}

	return false;
}

void UCombatEngagementSlotComponent::ReleaseSlot(AActor* Requester, bool bPreserveQueuePosition)
{
	if (!Requester || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	bool bReleasedReservation = false;
	for (TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
			bReleasedReservation = true;
		}
	}

	for (TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
			bReleasedReservation = true;
		}
	}

	for (TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		if (Reservation.Get() == Requester)
		{
			Reservation.Reset();
			bReleasedReservation = true;
		}
	}

	if (!bPreserveQueuePosition)
	{
		RemoveQueueRequester(Requester);
	}
	if (EngagementQueue.IsEmpty())
	{
		bInitialFormationActive = true;
		LastRequesterRegistrationTime = 0.0f;
	}
	if (bReleasedReservation && IsCorridorFormationActive())
	{
		RepackAllCorridorQueueLayers(true);
		++FormationRevision;
		NotifyAllReservedRequestersSlotChanged();
	}
}

bool UCombatEngagementSlotComponent::HandleStalledAttackReservation(
	AActor* Requester,
	float AttackReentryCooldown)
{
	if (!Requester || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	PruneInvalidReservations();
	int32 AttackSlotIndex = INDEX_NONE;
	if (!FindReservation(AttackReservations, Requester, AttackSlotIndex))
	{
		return false;
	}

	int32 WaitSlotIndex = INDEX_NONE;
	if (!FindBestWaitAdmissionForAttackSlot(AttackSlotIndex, WaitSlotIndex, true))
	{
		return false;
	}

	AActor* PromotedRequester = WaitReservations[WaitSlotIndex].Get();
	if (!PromotedRequester)
	{
		return false;
	}

	if (!ExecuteAttackWaitHandover(
		AttackSlotIndex,
		WaitSlotIndex,
		Requester,
		PromotedRequester,
		AttackReentryCooldown))
	{
		return false;
	}

	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s swapped stalled Attack owner %s with reachable Wait candidate %s."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		*Requester->GetName(),
		*PromotedRequester->GetName());
	return true;
}

void UCombatEngagementSlotComponent::InitializeSlots()
{
	AttackReservations.SetNum(FMath::Max(1, AttackSlotCount));
	AttackSlotVacantElapsed.SetNumZeroed(AttackReservations.Num());
	AttackHandoverCandidates.SetNum(AttackReservations.Num());
	AttackHandoverElapsed.SetNumZeroed(AttackReservations.Num());
	AttackHandoverBlockedUntil.SetNumZeroed(AttackReservations.Num());
	WaitReservations.SetNum(FMath::Max(1, WaitSlotCount));
	HoldingReservations.SetNum(FMath::Max(1, HoldingSlotCount));
}

void UCombatEngagementSlotComponent::UpdateEngagementAnchor(float DeltaTime)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const float SafeDeltaTime = FMath::Max(0.0f, DeltaTime);
	const float OwnerSpeed = Owner->GetVelocity().Size2D();
	const float StopSpeedThreshold = FMath::Max(0.0f, EngagementAnchorStopSpeedThreshold);
	const float ResumeSpeedThreshold = FMath::Max(StopSpeedThreshold, EngagementAnchorResumeSpeedThreshold);
	if (bEngagementAnchorRecentering)
	{
		if (OwnerSpeed >= ResumeSpeedThreshold)
		{
			bEngagementAnchorRecentering = false;
			EngagementAnchorStoppedElapsed = 0.0f;
		}
	}
	else if (OwnerSpeed <= StopSpeedThreshold)
	{
		EngagementAnchorStoppedElapsed += SafeDeltaTime;
		if (EngagementAnchorStoppedElapsed >= FMath::Max(0.0f, EngagementAnchorSettleDelay))
		{
			bEngagementAnchorRecentering = true;
		}
	}
	else
	{
		EngagementAnchorStoppedElapsed = 0.0f;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	FVector ToOwner = OwnerLocation - EngagementAnchorLocation;
	ToOwner.Z = 0.0f;
	const float Distance = ToOwner.Size2D();
	if (bEngagementAnchorRecentering)
	{
		const float SnapDistance = FMath::Max(0.0f, EngagementAnchorRecenterSnapDistance);
		const float RecenterDistance = FMath::Max(0.0f, EngagementAnchorRecenterSpeed) * SafeDeltaTime;
		if (Distance <= SnapDistance || RecenterDistance >= Distance)
		{
			EngagementAnchorLocation.X = OwnerLocation.X;
			EngagementAnchorLocation.Y = OwnerLocation.Y;
		}
		else if (!ToOwner.IsNearlyZero())
		{
			EngagementAnchorLocation += ToOwner.GetSafeNormal2D() * RecenterDistance;
		}
	}
	else
	{
		const float DeadZone = FMath::Max(0.0f, EngagementAnchorDeadZone);
		if (Distance > DeadZone && !ToOwner.IsNearlyZero())
		{
			const float FollowDistance = FMath::Min(
				Distance - DeadZone,
				FMath::Max(0.0f, EngagementAnchorFollowSpeed) * SafeDeltaTime);
			EngagementAnchorLocation += ToOwner.GetSafeNormal2D() * FollowDistance;
		}
	}
	EngagementAnchorLocation.Z = OwnerLocation.Z;
}

void UCombatEngagementSlotComponent::PruneInvalidReservations()
{
	bool bPrunedReservation = false;
	for (TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		if (!Reservation.IsValid())
		{
			bPrunedReservation |= Reservation.IsStale();
			Reservation.Reset();
		}
	}

	for (TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		if (!Reservation.IsValid())
		{
			bPrunedReservation |= Reservation.IsStale();
			Reservation.Reset();
		}
	}

	for (TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		if (!Reservation.IsValid())
		{
			bPrunedReservation |= Reservation.IsStale();
			Reservation.Reset();
		}
	}

	for (int32 QueueIndex = EngagementQueue.Num() - 1; QueueIndex >= 0; --QueueIndex)
	{
		if (!EngagementQueue[QueueIndex].Requester.IsValid())
		{
			bPrunedReservation = true;
			EngagementQueue.RemoveAtSwap(QueueIndex, 1, EAllowShrinking::No);
		}
	}

	if (EngagementQueue.IsEmpty())
	{
		bInitialFormationActive = true;
		LastRequesterRegistrationTime = 0.0f;
	}
	if (bPrunedReservation && IsCorridorFormationActive())
	{
		RepackAllCorridorQueueLayers(true);
		++FormationRevision;
		NotifyAllReservedRequestersSlotChanged();
	}
}

void UCombatEngagementSlotComponent::RefreshInitialFormationPhase()
{
	if (!bInitialFormationActive || EngagementQueue.IsEmpty())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World
		|| World->GetTimeSeconds() - LastRequesterRegistrationTime < InitialFormationSettleTime)
	{
		return;
	}

	FinalizeInitialFormationAssignments();
	bInitialFormationActive = false;
	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s completed congestion-aware initial engagement formation assignment."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"));
}

void UCombatEngagementSlotComponent::UpdateAttackVacancyTimers(float DeltaTime)
{
	if (AttackSlotVacantElapsed.Num() != AttackReservations.Num())
	{
		AttackSlotVacantElapsed.SetNumZeroed(AttackReservations.Num());
	}

	const int32 ActiveAttackSlotCount = GetActiveAttackSlotCount();
	const float SafeDeltaTime = FMath::Max(0.0f, DeltaTime);
	for (int32 SlotIndex = 0; SlotIndex < AttackReservations.Num(); ++SlotIndex)
	{
		const bool bTrackVacancy = !IsInitialFormationActive()
			&& SlotIndex < ActiveAttackSlotCount
			&& !AttackReservations[SlotIndex].IsValid()
			&& !IsAttackSlotBlockedByCurrentOccupancy(SlotIndex);
		AttackSlotVacantElapsed[SlotIndex] = bTrackVacancy
			? AttackSlotVacantElapsed[SlotIndex] + SafeDeltaTime
			: 0.0f;
	}
}

void UCombatEngagementSlotComponent::ResetAttackHandoverTracking(int32 AttackSlotIndex)
{
	if (AttackHandoverCandidates.IsValidIndex(AttackSlotIndex))
	{
		AttackHandoverCandidates[AttackSlotIndex].Reset();
	}
	if (AttackHandoverElapsed.IsValidIndex(AttackSlotIndex))
	{
		AttackHandoverElapsed[AttackSlotIndex] = 0.0f;
	}
}

void UCombatEngagementSlotComponent::ResetCoreIntrusionHandoverTracking()
{
	TrackedCoreIntrusionOwner.Reset();
	TrackedCoreIntruder.Reset();
	TrackedCoreIntrusionAttackSlot = INDEX_NONE;
	CoreIntrusionAnchorLocation = FVector::ZeroVector;
	CoreIntrusionStableElapsed = 0.0f;
}

bool UCombatEngagementSlotComponent::FindClosestCoreIntrusionPair(
	int32& OutAttackSlotIndex,
	AActor*& OutAttackOwner,
	AActor*& OutIntruder) const
{
	OutAttackSlotIndex = INDEX_NONE;
	OutAttackOwner = nullptr;
	OutIntruder = nullptr;
	const AActor* Owner = GetOwner();
	if (!bEnableCoreIntrusionHandover || !Owner)
	{
		return false;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const float CorridorRadiusSquared = FMath::Square(
		FMath::Max(0.0f, CoreIntrusionCorridorRadius));
	const float EndpointPadding = FMath::Max(0.0f, CoreIntrusionEndpointPadding);
	const float HeightTolerance = FMath::Max(0.0f, CoreIntrusionHeightTolerance);
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	float BestOwnerDistanceSquared = TNumericLimits<float>::Max();
	const int32 ActiveAttackCount = GetActiveAttackSlotCount();

	for (int32 AttackSlotIndex = 0;
		AttackSlotIndex < ActiveAttackCount && AttackReservations.IsValidIndex(AttackSlotIndex);
		++AttackSlotIndex)
	{
		AActor* AttackOwner = AttackReservations[AttackSlotIndex].Get();
		const ABHEnemy* AttackEnemy = Cast<ABHEnemy>(AttackOwner);
		if (!AttackEnemy || AttackEnemy->GetCombatState() != EBHEnemyCombatState::Chasing)
		{
			continue;
		}

		const FVector AttackOwnerLocation = AttackOwner->GetActorLocation();
		const FVector2D Segment = FVector2D(AttackOwnerLocation - OwnerLocation);
		const float SegmentLengthSquared = Segment.SizeSquared();
		const float SegmentLength = FMath::Sqrt(SegmentLengthSquared);
		if (SegmentLengthSquared <= UE_KINDA_SMALL_NUMBER
			|| SegmentLength <= EndpointPadding * 2.0f)
		{
			continue;
		}

		const float MinimumAlpha = EndpointPadding / SegmentLength;
		for (const FEngagementQueueEntry& Entry : EngagementQueue)
		{
			AActor* Intruder = Entry.Requester.Get();
			const ABHEnemy* IntruderEnemy = Cast<ABHEnemy>(Intruder);
			int32 ExistingAttackSlot = INDEX_NONE;
			if (!IntruderEnemy
				|| Intruder == AttackOwner
				|| IntruderEnemy->GetCombatState() != EBHEnemyCombatState::Chasing
				|| CurrentTime < Entry.AttackEligibleTime
				|| FindReservation(AttackReservations, Intruder, ExistingAttackSlot))
			{
				continue;
			}

			const FVector IntruderLocation = Intruder->GetActorLocation();
			if (FMath::Abs(IntruderLocation.Z - AttackOwnerLocation.Z) > HeightTolerance)
			{
				continue;
			}

			const FVector2D ToIntruder = FVector2D(IntruderLocation - OwnerLocation);
			const float Alpha = FVector2D::DotProduct(ToIntruder, Segment)
				/ SegmentLengthSquared;
			if (Alpha <= MinimumAlpha || Alpha >= 1.0f - MinimumAlpha)
			{
				continue;
			}

			const FVector2D ClosestPoint = FVector2D(OwnerLocation) + Segment * Alpha;
			if (FVector2D::DistSquared(FVector2D(IntruderLocation), ClosestPoint)
				> CorridorRadiusSquared)
			{
				continue;
			}

			const float OwnerDistanceSquared = FVector::DistSquared2D(
				IntruderLocation,
				AttackOwnerLocation);
			if (OwnerDistanceSquared < BestOwnerDistanceSquared)
			{
				BestOwnerDistanceSquared = OwnerDistanceSquared;
				OutAttackSlotIndex = AttackSlotIndex;
				OutAttackOwner = AttackOwner;
				OutIntruder = Intruder;
			}
		}
	}

	return OutAttackSlotIndex != INDEX_NONE && OutAttackOwner && OutIntruder;
}

bool UCombatEngagementSlotComponent::ExecuteCoreIntrusionHandover(
	int32 AttackSlotIndex,
	AActor* AttackOwner,
	AActor* Intruder)
{
	if (!AttackReservations.IsValidIndex(AttackSlotIndex)
		|| AttackReservations[AttackSlotIndex].Get() != AttackOwner
		|| !AttackOwner
		|| !Intruder)
	{
		return false;
	}

	int32 IntruderLayerIndex = INDEX_NONE;
	const bool bIntruderWasWait = FindReservation(
		WaitReservations,
		Intruder,
		IntruderLayerIndex);
	const bool bIntruderWasHolding = !bIntruderWasWait
		&& FindReservation(HoldingReservations, Intruder, IntruderLayerIndex);
	if (!bIntruderWasWait && !bIntruderWasHolding)
	{
		int32 PendingIndex = INDEX_NONE;
		if (!FindPendingRequesterIndex(Intruder, PendingIndex))
		{
			return false;
		}
	}
	if (!CanRequesterOccupyAttackSlot(Intruder, AttackSlotIndex, AttackOwner))
	{
		return false;
	}

	AttackReservations[AttackSlotIndex] = Intruder;
	if (bIntruderWasWait && WaitReservations.IsValidIndex(IntruderLayerIndex))
	{
		WaitReservations[IntruderLayerIndex] = AttackOwner;
	}
	else if (bIntruderWasHolding && HoldingReservations.IsValidIndex(IntruderLayerIndex))
	{
		HoldingReservations[IntruderLayerIndex] = AttackOwner;
	}
	// A Pending intruder owns no ring reservation. Its queue registration moves
	// to Attack, while the former owner becomes Pending without losing sequence.

	UpdateCorridorSideForAttackReservation(Intruder, AttackSlotIndex);
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const float Cooldown = FMath::Max(0.0f, CoreIntrusionHandoverCooldown);
	SetAttackEligibleTime(AttackOwner, CurrentTime + Cooldown);
	if (AttackHandoverBlockedUntil.IsValidIndex(AttackSlotIndex))
	{
		AttackHandoverBlockedUntil[AttackSlotIndex] = CurrentTime + Cooldown;
	}
	ResetAttackHandoverTracking(AttackSlotIndex);

	NotifyRequesterSlotChanged(Intruder);
	NotifyRequesterSlotChanged(AttackOwner);
	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s yielded Attack slot %d from %s to core intruder %s."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		AttackSlotIndex,
		*AttackOwner->GetName(),
		*Intruder->GetName());
	return true;
}

void UCombatEngagementSlotComponent::ActivatePocketRapidReform()
{
	if (!IsPocketFormationActive() || !GetWorld())
	{
		return;
	}
	PocketRapidReformUntil = FMath::Max(
		PocketRapidReformUntil,
		GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, PocketRapidReformDuration));
}

bool UCombatEngagementSlotComponent::IsPocketRapidReformActive() const
{
	return IsPocketFormationActive()
		&& GetWorld()
		&& GetWorld()->GetTimeSeconds() < PocketRapidReformUntil;
}

void UCombatEngagementSlotComponent::RefreshCoreIntrusionHandover(float DeltaTime)
{
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (!bEnableCoreIntrusionHandover || CurrentTime < CoreIntrusionBlockedUntil)
	{
		ResetCoreIntrusionHandoverTracking();
		return;
	}

	int32 AttackSlotIndex = INDEX_NONE;
	AActor* AttackOwner = nullptr;
	AActor* Intruder = nullptr;
	if (!FindClosestCoreIntrusionPair(AttackSlotIndex, AttackOwner, Intruder))
	{
		ResetCoreIntrusionHandoverTracking();
		return;
	}

	if (TrackedCoreIntrusionAttackSlot != AttackSlotIndex
		|| TrackedCoreIntrusionOwner.Get() != AttackOwner
		|| TrackedCoreIntruder.Get() != Intruder)
	{
		TrackedCoreIntrusionAttackSlot = AttackSlotIndex;
		TrackedCoreIntrusionOwner = AttackOwner;
		TrackedCoreIntruder = Intruder;
		CoreIntrusionAnchorLocation = Intruder->GetActorLocation();
		CoreIntrusionStableElapsed = 0.0f;
		return;
	}

	if (FVector::DistSquared2D(Intruder->GetActorLocation(), CoreIntrusionAnchorLocation)
		> FMath::Square(FMath::Max(0.0f, CoreIntrusionProgressDistance)))
	{
		CoreIntrusionAnchorLocation = Intruder->GetActorLocation();
		CoreIntrusionStableElapsed = 0.0f;
		return;
	}

	CoreIntrusionStableElapsed += FMath::Max(0.0f, DeltaTime);
	if (CoreIntrusionStableElapsed < FMath::Max(0.0f, CoreIntrusionStableTime))
	{
		return;
	}

	if (ExecuteCoreIntrusionHandover(AttackSlotIndex, AttackOwner, Intruder))
	{
		CoreIntrusionBlockedUntil = CurrentTime
			+ FMath::Max(0.0f, CoreIntrusionHandoverCooldown);
	}
	ResetCoreIntrusionHandoverTracking();
}

bool UCombatEngagementSlotComponent::FindFastAttackHandoverCandidate(
	int32 AttackSlotIndex,
	AActor* CurrentAttackOwner,
	int32& OutWaitSlotIndex) const
{
	OutWaitSlotIndex = INDEX_NONE;
	const ABHEnemy* CurrentEnemy = Cast<ABHEnemy>(CurrentAttackOwner);
	if (!CurrentEnemy
		|| CurrentEnemy->GetCombatState() != EBHEnemyCombatState::Chasing
		|| !AttackReservations.IsValidIndex(AttackSlotIndex)
		|| AttackSlotIndex >= GetActiveAttackSlotCount())
	{
		return false;
	}

	FVector AttackSlotLocation;
	if (!GetSlotWorldLocation(
		EBHCombatSlotType::Attack,
		AttackSlotIndex,
		AttackSlotLocation))
	{
		return false;
	}

	const float OwnerDistance = FVector::Dist2D(
		CurrentAttackOwner->GetActorLocation(),
		AttackSlotLocation);
	if (OwnerDistance < FMath::Max(0.0f, AttackHandoverOwnerDistance))
	{
		return false;
	}

	const float RequiredAdvantage = FMath::Max(
		0.0f,
		IsPocketRapidReformActive()
			? PocketRapidHandoverDistanceAdvantage
			: AttackHandoverDistanceAdvantage);
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	float BestDistance = TNumericLimits<float>::Max();
	uint64 BestSequence = TNumericLimits<uint64>::Max();
	for (int32 WaitSlotIndex = 0; WaitSlotIndex < WaitReservations.Num(); ++WaitSlotIndex)
	{
		AActor* Candidate = WaitReservations[WaitSlotIndex].Get();
		const ABHEnemy* CandidateEnemy = Cast<ABHEnemy>(Candidate);
		if (!CandidateEnemy
			|| CandidateEnemy->GetCombatState() != EBHEnemyCombatState::Chasing
			|| CurrentTime < GetAttackEligibleTime(Candidate)
			|| (IsCorridorFormationActive()
				&& (!CorridorWaitRowLayout.IsValidIndex(WaitSlotIndex)
					|| !IsCorridorAttackSlotOnRequesterSide(Candidate, AttackSlotIndex))))
		{
			continue;
		}

		const float CandidateDistance = FVector::Dist2D(
			Candidate->GetActorLocation(),
			AttackSlotLocation);
		if (OwnerDistance - CandidateDistance < RequiredAdvantage)
		{
			continue;
		}

		const float MaximumAttackSlotDistance = GetMaximumAttackSlotDistance(Candidate);
		if (FVector::DistSquared2D(GetOwner()->GetActorLocation(), AttackSlotLocation)
			> FMath::Square(MaximumAttackSlotDistance))
		{
			continue;
		}

		const uint64 Sequence = GetQueueSequence(Candidate);
		const bool bCloser = CandidateDistance + 1.0f < BestDistance;
		const bool bSameDistanceOlder = FMath::IsNearlyEqual(
			CandidateDistance,
			BestDistance,
			1.0f) && Sequence < BestSequence;
		if (OutWaitSlotIndex == INDEX_NONE || bCloser || bSameDistanceOlder)
		{
			OutWaitSlotIndex = WaitSlotIndex;
			BestDistance = CandidateDistance;
			BestSequence = Sequence;
		}
	}

	return OutWaitSlotIndex != INDEX_NONE;
}

bool UCombatEngagementSlotComponent::ExecuteAttackWaitHandover(
	int32 AttackSlotIndex,
	int32 WaitSlotIndex,
	AActor* CurrentAttackOwner,
	AActor* Candidate,
	float Cooldown)
{
	if (!AttackReservations.IsValidIndex(AttackSlotIndex)
		|| !WaitReservations.IsValidIndex(WaitSlotIndex)
		|| AttackReservations[AttackSlotIndex].Get() != CurrentAttackOwner
		|| WaitReservations[WaitSlotIndex].Get() != Candidate
		|| !CurrentAttackOwner
		|| !Candidate)
	{
		return false;
	}
	if (!CanRequesterOccupyAttackSlot(Candidate, AttackSlotIndex, CurrentAttackOwner))
	{
		return false;
	}

	FVector AttackSlotLocation;
	FVector FormerWaitSlotLocation;
	float CandidatePathScore = 0.0f;
	float FormerOwnerPathScore = 0.0f;
	if (!GetSlotWorldLocation(
		EBHCombatSlotType::Attack,
		AttackSlotIndex,
		AttackSlotLocation)
		|| !GetSlotWorldLocation(
			EBHCombatSlotType::Wait,
			WaitSlotIndex,
			FormerWaitSlotLocation)
		|| !GetNavigationPathScore(Candidate, AttackSlotLocation, CandidatePathScore))
	{
		return false;
	}

	const bool bFormerOwnerCanReachWait = GetNavigationPathScore(
		CurrentAttackOwner,
		FormerWaitSlotLocation,
		FormerOwnerPathScore);
	AttackReservations[AttackSlotIndex] = Candidate;
	WaitReservations[WaitSlotIndex] = bFormerOwnerCanReachWait
		? CurrentAttackOwner
		: nullptr;
	UpdateCorridorSideForAttackReservation(Candidate, AttackSlotIndex);

	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const float SafeCooldown = FMath::Max(0.0f, Cooldown);
	SetAttackEligibleTime(CurrentAttackOwner, CurrentTime + SafeCooldown);
	if (AttackHandoverBlockedUntil.IsValidIndex(AttackSlotIndex))
	{
		AttackHandoverBlockedUntil[AttackSlotIndex] = CurrentTime + SafeCooldown;
	}
	ResetAttackHandoverTracking(AttackSlotIndex);

	if (IsCorridorFormationActive())
	{
		RepackCorridorLayerReservations(
			WaitReservations,
			EBHCombatSlotType::Wait,
			true);
	}
	NotifyRequesterSlotChanged(Candidate);
	NotifyRequesterSlotChanged(CurrentAttackOwner);
	if (!bFormerOwnerCanReachWait)
	{
		UE_LOG(
			LogProjectBH,
			Display,
			TEXT("%s demoted unreachable Attack owner %s to Pending after handing slot %d to %s."),
			GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
			*CurrentAttackOwner->GetName(),
			AttackSlotIndex,
			*Candidate->GetName());
	}
	return true;
}

void UCombatEngagementSlotComponent::RefreshFastAttackHandovers(float DeltaTime)
{
	if (AttackHandoverCandidates.Num() != AttackReservations.Num()
		|| AttackHandoverElapsed.Num() != AttackReservations.Num()
		|| AttackHandoverBlockedUntil.Num() != AttackReservations.Num())
	{
		AttackHandoverCandidates.SetNum(AttackReservations.Num());
		AttackHandoverElapsed.SetNumZeroed(AttackReservations.Num());
		AttackHandoverBlockedUntil.SetNumZeroed(AttackReservations.Num());
	}

	const int32 ActiveAttackSlotCount = GetActiveAttackSlotCount();
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const float SafeDeltaTime = FMath::Max(0.0f, DeltaTime);
	// CoreEscape is requester-local. One enemy escaping the player core must not
	// freeze unrelated Attack handovers on the opposite side of the formation.
	const bool bHandoverGloballyBlocked = IsInitialFormationActive();

	for (int32 AttackSlotIndex = 0;
		AttackSlotIndex < AttackReservations.Num();
		++AttackSlotIndex)
	{
		AActor* CurrentAttackOwner = AttackReservations[AttackSlotIndex].Get();
		if (bHandoverGloballyBlocked
			|| AttackSlotIndex >= ActiveAttackSlotCount
			|| !CurrentAttackOwner
			|| (AttackHandoverBlockedUntil.IsValidIndex(AttackSlotIndex)
				&& CurrentTime < AttackHandoverBlockedUntil[AttackSlotIndex]))
		{
			ResetAttackHandoverTracking(AttackSlotIndex);
			continue;
		}

		int32 WaitSlotIndex = INDEX_NONE;
		if (!FindFastAttackHandoverCandidate(
			AttackSlotIndex,
			CurrentAttackOwner,
			WaitSlotIndex))
		{
			ResetAttackHandoverTracking(AttackSlotIndex);
			continue;
		}

		AActor* Candidate = WaitReservations.IsValidIndex(WaitSlotIndex)
			? WaitReservations[WaitSlotIndex].Get()
			: nullptr;
		if (!Candidate)
		{
			ResetAttackHandoverTracking(AttackSlotIndex);
			continue;
		}

		if (AttackHandoverCandidates[AttackSlotIndex].Get() != Candidate)
		{
			AttackHandoverCandidates[AttackSlotIndex] = Candidate;
			AttackHandoverElapsed[AttackSlotIndex] = SafeDeltaTime;
			continue;
		}

		AttackHandoverElapsed[AttackSlotIndex] += SafeDeltaTime;
		const float RequiredDelay = IsPocketRapidReformActive()
			? PocketRapidHandoverDelay
			: AttackHandoverDelay;
		if (AttackHandoverElapsed[AttackSlotIndex]
			< FMath::Max(0.0f, RequiredDelay))
		{
			continue;
		}

		FVector AttackSlotLocation;
		float CandidatePathScore = 0.0f;
		if (!GetSlotWorldLocation(
			EBHCombatSlotType::Attack,
			AttackSlotIndex,
			AttackSlotLocation)
			|| !GetNavigationPathScore(Candidate, AttackSlotLocation, CandidatePathScore))
		{
			int32 ReachableWaitSlotIndex = INDEX_NONE;
			if (!FindBestWaitAdmissionForAttackSlot(
				AttackSlotIndex,
				ReachableWaitSlotIndex,
				true))
			{
				AttackHandoverBlockedUntil[AttackSlotIndex] = CurrentTime + 0.25f;
				ResetAttackHandoverTracking(AttackSlotIndex);
				continue;
			}
			WaitSlotIndex = ReachableWaitSlotIndex;
			Candidate = WaitReservations.IsValidIndex(WaitSlotIndex)
				? WaitReservations[WaitSlotIndex].Get()
				: nullptr;
		}

		const float Cooldown = FMath::Max(0.0f, AttackHandoverCooldown);
		if (!ExecuteAttackWaitHandover(
			AttackSlotIndex,
			WaitSlotIndex,
			CurrentAttackOwner,
			Candidate,
			Cooldown))
		{
			AttackHandoverBlockedUntil[AttackSlotIndex] = CurrentTime + 0.25f;
			ResetAttackHandoverTracking(AttackSlotIndex);
			continue;
		}

		UE_LOG(
			LogProjectBH,
			Display,
			TEXT("%s handed Attack slot %d from %s to closer Wait candidate %s."),
			GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
			AttackSlotIndex,
			*CurrentAttackOwner->GetName(),
			*Candidate->GetName());
	}
}

void UCombatEngagementSlotComponent::FinalizeInitialFormationAssignments()
{
	for (TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		Reservation.Reset();
	}
	for (TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		Reservation.Reset();
	}
	for (TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		Reservation.Reset();
	}

	AActor* Requester = nullptr;
	int32 AttackSlotIndex = INDEX_NONE;
	while (FindBestInitialAttackAssignment(Requester, AttackSlotIndex))
	{
		AttackReservations[AttackSlotIndex] = Requester;
		UpdateCorridorSideForAttackReservation(Requester, AttackSlotIndex);
	}

	while (PromoteBestOuterRequesterToWait())
	{
	}

	while (AssignBestPendingRequesterToHolding())
	{
	}

	++FormationRevision;
	NotifyAllReservedRequestersSlotChanged();
}

void UCombatEngagementSlotComponent::NotifyRequesterSlotChanged(AActor* Requester) const
{
	const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
	if (ABHCrowdEnemyAIController* Controller = Enemy
		? Cast<ABHCrowdEnemyAIController>(Enemy->GetController())
		: nullptr)
	{
		Controller->NotifyCombatSlotAssignmentChanged();
	}
}

void UCombatEngagementSlotComponent::NotifyAllReservedRequestersSlotChanged() const
{
	for (const TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		NotifyRequesterSlotChanged(Reservation.Get());
	}
	for (const TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		NotifyRequesterSlotChanged(Reservation.Get());
	}
	for (const TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		NotifyRequesterSlotChanged(Reservation.Get());
	}
}

void UCombatEngagementSlotComponent::RefreshPromotions()
{
	// CoreEscape is requester-local. A single crowded intruder must not freeze
	// every vacant Attack slot: other Wait owners can fill the ring while the
	// intruder keeps its outward waypoint. Enemy capsules do not block each other,
	// and ResolveCombatCoreEscapeGoal already scores occupied escape corridors.
	while (PromoteBestWaitReservationToAttack(false))
	{
	}
	while (PromoteBestWaitReservationToAttack(true))
	{
	}

	// A vacant Wait slot is filled directly from the best reachable outer requester.
	// Requiring a Holding owner to first arrive at its blue slot creates a visible
	// stop-and-go waypoint before it is immediately sent inward again.
	while (PromoteBestOuterRequesterToWait())
	{
	}

	while (AssignBestPendingRequesterToHolding())
	{
	}
}

bool UCombatEngagementSlotComponent::PromoteBestWaitReservationToAttack(bool bAllowUnarrivedWait)
{
	int32 WaitSlotIndex = INDEX_NONE;
	int32 AttackSlotIndex = INDEX_NONE;
	if (!FindBestWaitAdmission(WaitSlotIndex, AttackSlotIndex, bAllowUnarrivedWait))
	{
		return false;
	}

	AActor* Requester = WaitReservations[WaitSlotIndex].Get();
	if (!Requester)
	{
		return false;
	}

	WaitReservations[WaitSlotIndex].Reset();
	AttackReservations[AttackSlotIndex] = Requester;
	if (AttackSlotVacantElapsed.IsValidIndex(AttackSlotIndex))
	{
		AttackSlotVacantElapsed[AttackSlotIndex] = 0.0f;
	}
	UpdateCorridorSideForAttackReservation(Requester, AttackSlotIndex);
	if (IsCorridorFormationActive())
	{
		RepackCorridorLayerReservations(
			WaitReservations,
			EBHCombatSlotType::Wait,
			true);
	}
	NotifyRequesterSlotChanged(Requester);
	const TCHAR* AdmissionRule = bAllowUnarrivedWait
		? TEXT("vacancy fallback")
		: (IsCorridorFormationActive()
			? TEXT("side-preferred reachable queue rule")
			: TEXT("congestion-aware path cost"));
	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s admitted %s from Wait to Attack slot %d by %s."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		*Requester->GetName(),
		AttackSlotIndex,
		AdmissionRule);
	return true;
}

bool UCombatEngagementSlotComponent::PromoteBestOuterRequesterToWait()
{
	AActor* BestRequester = nullptr;
	int32 BestWaitSlotIndex = INDEX_NONE;
	int32 BestHoldingSlotIndex = INDEX_NONE;
	float BestPathScore = TNumericLimits<float>::Max();
	uint64 BestSequence = TNumericLimits<uint64>::Max();
	for (int32 WaitSlotIndex = 0; WaitSlotIndex < WaitReservations.Num(); ++WaitSlotIndex)
	{
		if (WaitReservations[WaitSlotIndex].IsValid())
		{
			continue;
		}

		FVector WaitSlotLocation;
		if (!GetSlotWorldLocation(
			EBHCombatSlotType::Wait,
			WaitSlotIndex,
			WaitSlotLocation))
		{
			continue;
		}

		for (const FEngagementQueueEntry& Entry : EngagementQueue)
		{
			AActor* Requester = Entry.Requester.Get();
			int32 AttackSlotIndex = INDEX_NONE;
			int32 ExistingWaitSlotIndex = INDEX_NONE;
			if (!Requester
				|| FindReservation(AttackReservations, Requester, AttackSlotIndex)
				|| FindReservation(WaitReservations, Requester, ExistingWaitSlotIndex))
			{
				continue;
			}
			if (IsCorridorFormationActive()
				&& (!CorridorWaitRowLayout.IsValidIndex(WaitSlotIndex)
					|| CorridorWaitRowLayout[WaitSlotIndex].SideIndex
						!= GetCorridorSideIndex(Requester)))
			{
				continue;
			}

			float PathScore = 0.0f;
			if (!GetNavigationPathScore(Requester, WaitSlotLocation, PathScore))
			{
				continue;
			}

			const uint64 Sequence = GetQueueSequence(Requester);
			const bool bBetterPath = PathScore + 1.0f < BestPathScore;
			const bool bSamePathOlder = FMath::IsNearlyEqual(
				PathScore,
				BestPathScore,
				1.0f) && Sequence < BestSequence;
			if (!BestRequester || bBetterPath || bSamePathOlder)
			{
				BestRequester = Requester;
				BestWaitSlotIndex = WaitSlotIndex;
				BestPathScore = PathScore;
				BestSequence = Sequence;
				BestHoldingSlotIndex = INDEX_NONE;
				FindReservation(
					HoldingReservations,
					Requester,
					BestHoldingSlotIndex);
			}
		}
	}

	if (!BestRequester || !WaitReservations.IsValidIndex(BestWaitSlotIndex))
	{
		return false;
	}
	if (HoldingReservations.IsValidIndex(BestHoldingSlotIndex)
		&& HoldingReservations[BestHoldingSlotIndex].Get() == BestRequester)
	{
		HoldingReservations[BestHoldingSlotIndex].Reset();
	}
	WaitReservations[BestWaitSlotIndex] = BestRequester;
	NotifyRequesterSlotChanged(BestRequester);

	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s routed %s directly from the outer reserve to Wait slot %d by path cost."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		*BestRequester->GetName(),
		BestWaitSlotIndex);
	return true;
}

bool UCombatEngagementSlotComponent::AssignBestPendingRequesterToHolding()
{
	AActor* BestRequester = nullptr;
	int32 BestHoldingSlotIndex = INDEX_NONE;
	float BestPathScore = TNumericLimits<float>::Max();
	uint64 BestSequence = TNumericLimits<uint64>::Max();
	for (int32 HoldingSlotIndex = 0;
		HoldingSlotIndex < HoldingReservations.Num();
		++HoldingSlotIndex)
	{
		if (HoldingReservations[HoldingSlotIndex].IsValid())
		{
			continue;
		}

		FVector HoldingSlotLocation;
		if (!GetSlotWorldLocation(
			EBHCombatSlotType::Holding,
			HoldingSlotIndex,
			HoldingSlotLocation))
		{
			continue;
		}

		for (const FEngagementQueueEntry& Entry : EngagementQueue)
		{
			AActor* Requester = Entry.Requester.Get();
			if (!Requester || IsRequesterReserved(Requester))
			{
				continue;
			}
			if (IsCorridorFormationActive()
				&& (!CorridorHoldingRowLayout.IsValidIndex(HoldingSlotIndex)
					|| CorridorHoldingRowLayout[HoldingSlotIndex].SideIndex
						!= GetCorridorSideIndex(Requester)))
			{
				continue;
			}

			float PathScore = 0.0f;
			if (!GetNavigationPathScore(Requester, HoldingSlotLocation, PathScore))
			{
				continue;
			}

			const uint64 Sequence = GetQueueSequence(Requester);
			const bool bBetterPath = PathScore + 1.0f < BestPathScore;
			const bool bSamePathOlder = FMath::IsNearlyEqual(
				PathScore,
				BestPathScore,
				1.0f) && Sequence < BestSequence;
			if (!BestRequester || bBetterPath || bSamePathOlder)
			{
				BestRequester = Requester;
				BestHoldingSlotIndex = HoldingSlotIndex;
				BestPathScore = PathScore;
				BestSequence = Sequence;
			}
		}
	}

	if (!BestRequester || !HoldingReservations.IsValidIndex(BestHoldingSlotIndex))
	{
		return false;
	}
	HoldingReservations[BestHoldingSlotIndex] = BestRequester;
	NotifyRequesterSlotChanged(BestRequester);
	return true;
}

bool UCombatEngagementSlotComponent::FindBestWaitAdmission(
	int32& OutWaitSlotIndex,
	int32& OutAttackSlotIndex,
	bool bAllowUnarrivedWait) const
{
	OutWaitSlotIndex = INDEX_NONE;
	OutAttackSlotIndex = INDEX_NONE;
	if (IsCorridorFormationActive())
	{
		uint64 BestSequence = TNumericLimits<uint64>::Max();
		for (int32 AttackSlotIndex = 0;
			AttackSlotIndex < GetActiveAttackSlotCount();
			++AttackSlotIndex)
		{
			if (AttackReservations[AttackSlotIndex].IsValid())
			{
				continue;
			}
			if (bAllowUnarrivedWait
				&& (!AttackSlotVacantElapsed.IsValidIndex(AttackSlotIndex)
					|| AttackSlotVacantElapsed[AttackSlotIndex]
						< FMath::Max(0.0f, AttackVacancyFallbackDelay)))
			{
				continue;
			}

			int32 WaitSlotIndex = INDEX_NONE;
			if (!FindCorridorWaitAdmissionForAttackSlot(
				AttackSlotIndex,
				WaitSlotIndex,
				!bAllowUnarrivedWait))
			{
				continue;
			}

			const uint64 Sequence = GetQueueSequence(WaitReservations[WaitSlotIndex].Get());
			if (OutWaitSlotIndex == INDEX_NONE || Sequence < BestSequence)
			{
				OutWaitSlotIndex = WaitSlotIndex;
				OutAttackSlotIndex = AttackSlotIndex;
				BestSequence = Sequence;
			}
		}
		return OutWaitSlotIndex != INDEX_NONE;
	}

	float BestPathScore = TNumericLimits<float>::Max();
	uint64 BestSequence = TNumericLimits<uint64>::Max();
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	for (int32 AttackSlotIndex = 0; AttackSlotIndex < GetActiveAttackSlotCount(); ++AttackSlotIndex)
	{
		if (AttackReservations[AttackSlotIndex].IsValid())
		{
			continue;
		}
		if (bAllowUnarrivedWait
			&& (!AttackSlotVacantElapsed.IsValidIndex(AttackSlotIndex)
				|| AttackSlotVacantElapsed[AttackSlotIndex]
					< FMath::Max(0.0f, AttackVacancyFallbackDelay)))
		{
			continue;
		}

		FVector AttackSlotLocation;
		if (!GetSlotWorldLocation(EBHCombatSlotType::Attack, AttackSlotIndex, AttackSlotLocation))
		{
			continue;
		}

		for (int32 WaitSlotIndex = 0; WaitSlotIndex < WaitReservations.Num(); ++WaitSlotIndex)
		{
			AActor* Requester = WaitReservations[WaitSlotIndex].Get();
			if (!Requester || CurrentTime < GetAttackEligibleTime(Requester))
			{
				continue;
			}
			if (!CanRequesterOccupyAttackSlot(Requester, AttackSlotIndex))
			{
				continue;
			}

			FVector WaitSlotLocation;
			if (!GetSlotWorldLocation(EBHCombatSlotType::Wait, WaitSlotIndex, WaitSlotLocation)
				|| (!bAllowUnarrivedWait
					&& FVector::DistSquared2D(Requester->GetActorLocation(), WaitSlotLocation)
						> FMath::Square(PromotionArrivalRadius)))
			{
				continue;
			}

			const float MaximumAttackSlotDistance = GetMaximumAttackSlotDistance(Requester);
			if (FVector::DistSquared2D(GetOwner()->GetActorLocation(), AttackSlotLocation)
				> FMath::Square(MaximumAttackSlotDistance))
			{
				continue;
			}

			float PathScore = 0.0f;
			if (!GetNavigationPathScore(Requester, AttackSlotLocation, PathScore))
			{
				continue;
			}

			const uint64 Sequence = GetQueueSequence(Requester);
			const bool bBetterPath = PathScore + 1.0f < BestPathScore;
			const bool bSamePathOlder = FMath::IsNearlyEqual(PathScore, BestPathScore, 1.0f)
				&& Sequence < BestSequence;
			if (OutWaitSlotIndex == INDEX_NONE || bBetterPath || bSamePathOlder)
			{
				BestPathScore = PathScore;
				BestSequence = Sequence;
				OutWaitSlotIndex = WaitSlotIndex;
				OutAttackSlotIndex = AttackSlotIndex;
			}
		}
	}

	return OutWaitSlotIndex != INDEX_NONE && OutAttackSlotIndex != INDEX_NONE;
}

bool UCombatEngagementSlotComponent::FindBestWaitAdmissionForAttackSlot(
	int32 AttackSlotIndex,
	int32& OutWaitSlotIndex,
	bool bAllowUnarrivedWait) const
{
	OutWaitSlotIndex = INDEX_NONE;
	if (IsCorridorFormationActive())
	{
		return FindCorridorWaitAdmissionForAttackSlot(
			AttackSlotIndex,
			OutWaitSlotIndex,
			!bAllowUnarrivedWait);
	}
	if (!AttackReservations.IsValidIndex(AttackSlotIndex))
	{
		return false;
	}

	FVector AttackSlotLocation;
	if (!GetSlotWorldLocation(EBHCombatSlotType::Attack, AttackSlotIndex, AttackSlotLocation))
	{
		return false;
	}

	float BestPathScore = TNumericLimits<float>::Max();
	uint64 BestSequence = TNumericLimits<uint64>::Max();
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	for (int32 WaitSlotIndex = 0; WaitSlotIndex < WaitReservations.Num(); ++WaitSlotIndex)
	{
		AActor* Requester = WaitReservations[WaitSlotIndex].Get();
		if (!Requester || CurrentTime < GetAttackEligibleTime(Requester))
		{
			continue;
		}
		if (!CanRequesterOccupyAttackSlot(Requester, AttackSlotIndex))
		{
			continue;
		}

		FVector WaitSlotLocation;
		if (!GetSlotWorldLocation(EBHCombatSlotType::Wait, WaitSlotIndex, WaitSlotLocation)
			|| (!bAllowUnarrivedWait
				&& FVector::DistSquared2D(Requester->GetActorLocation(), WaitSlotLocation)
					> FMath::Square(PromotionArrivalRadius)))
		{
			continue;
		}

		const float MaximumAttackSlotDistance = GetMaximumAttackSlotDistance(Requester);
		if (FVector::DistSquared2D(GetOwner()->GetActorLocation(), AttackSlotLocation)
			> FMath::Square(MaximumAttackSlotDistance))
		{
			continue;
		}

		float PathScore = 0.0f;
		if (!GetNavigationPathScore(Requester, AttackSlotLocation, PathScore))
		{
			continue;
		}

		const uint64 Sequence = GetQueueSequence(Requester);
		const bool bBetterPath = PathScore + 1.0f < BestPathScore;
		const bool bSamePathOlder = FMath::IsNearlyEqual(PathScore, BestPathScore, 1.0f)
			&& Sequence < BestSequence;
		if (OutWaitSlotIndex == INDEX_NONE || bBetterPath || bSamePathOlder)
		{
			BestPathScore = PathScore;
			BestSequence = Sequence;
			OutWaitSlotIndex = WaitSlotIndex;
		}
	}

	return OutWaitSlotIndex != INDEX_NONE;
}

bool UCombatEngagementSlotComponent::FindBestInitialAttackAssignment(
	AActor*& OutRequester,
	int32& OutAttackSlotIndex) const
{
	OutRequester = nullptr;
	OutAttackSlotIndex = INDEX_NONE;
	float BestPathScore = TNumericLimits<float>::Max();
	uint64 BestSequence = TNumericLimits<uint64>::Max();
	bool bBestUsesPreferredSide = false;
	bool bBestUsesPreferredChannel = false;
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	for (int32 AttackSlotIndex = 0; AttackSlotIndex < GetActiveAttackSlotCount(); ++AttackSlotIndex)
	{
		if (AttackReservations[AttackSlotIndex].IsValid())
		{
			continue;
		}

		FVector AttackSlotLocation;
		if (!GetSlotWorldLocation(EBHCombatSlotType::Attack, AttackSlotIndex, AttackSlotLocation))
		{
			continue;
		}

		for (const FEngagementQueueEntry& Entry : EngagementQueue)
		{
			AActor* Requester = Entry.Requester.Get();
			if (!Requester || IsRequesterReserved(Requester) || CurrentTime < Entry.AttackEligibleTime)
			{
				continue;
			}
			if (!CanRequesterOccupyAttackSlot(Requester, AttackSlotIndex))
			{
				continue;
			}
			if (IsCorridorFormationActive()
				&& !IsCorridorAttackSlotOnRequesterSide(Requester, AttackSlotIndex))
			{
				continue;
			}
			const float MaximumAttackSlotDistance = GetMaximumAttackSlotDistance(Requester);
			if (FVector::DistSquared2D(GetOwner()->GetActorLocation(), AttackSlotLocation)
				> FMath::Square(MaximumAttackSlotDistance))
			{
				continue;
			}

			float PathScore = 0.0f;
			if (!GetNavigationPathScore(Requester, AttackSlotLocation, PathScore))
			{
				continue;
			}

			const bool bUsesPreferredSide = IsCorridorFormationActive()
				&& GetCorridorSideIndex(Requester) == GetCorridorAttackSlotSideIndex(AttackSlotIndex);
			const bool bUsesPreferredChannel = IsCorridorFormationActive()
				&& GetCorridorQueueChannelIndex(Requester)
					== GetCorridorAttackSlotChannelIndex(AttackSlotIndex);
			const bool bOlderRequester = Entry.Sequence < BestSequence;
			const bool bSameRequesterPreferredSide = Entry.Sequence == BestSequence
				&& bUsesPreferredSide && !bBestUsesPreferredSide;
			const bool bSameRequesterPreferredChannel = Entry.Sequence == BestSequence
				&& bUsesPreferredSide == bBestUsesPreferredSide
				&& bUsesPreferredChannel && !bBestUsesPreferredChannel;
			const bool bSameRequesterBetterPath = Entry.Sequence == BestSequence
				&& bUsesPreferredSide == bBestUsesPreferredSide
				&& bUsesPreferredChannel == bBestUsesPreferredChannel
				&& PathScore < BestPathScore;
			const bool bBetterOpenOrPocketPath = !IsCorridorFormationActive()
				&& (PathScore + 1.0f < BestPathScore
					|| (FMath::IsNearlyEqual(PathScore, BestPathScore, 1.0f)
						&& Entry.Sequence < BestSequence));
			const bool bBetterCorridorAssignment = IsCorridorFormationActive()
				&& (bOlderRequester
					|| bSameRequesterPreferredSide
					|| bSameRequesterPreferredChannel
					|| bSameRequesterBetterPath);
			if (!OutRequester || bBetterOpenOrPocketPath || bBetterCorridorAssignment)
			{
				BestPathScore = PathScore;
				BestSequence = Entry.Sequence;
				bBestUsesPreferredSide = bUsesPreferredSide;
				bBestUsesPreferredChannel = bUsesPreferredChannel;
				OutRequester = Requester;
				OutAttackSlotIndex = AttackSlotIndex;
			}
		}
	}

	return OutRequester != nullptr && OutAttackSlotIndex != INDEX_NONE;
}

bool UCombatEngagementSlotComponent::FindPendingRequesterIndex(
	AActor* Requester,
	int32& OutPendingIndex) const
{
	OutPendingIndex = INDEX_NONE;
	const uint64 RequesterSequence = GetQueueSequence(Requester);
	if (RequesterSequence == TNumericLimits<uint64>::Max())
	{
		return false;
	}

	int32 PendingBeforeRequester = 0;
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* OtherRequester = Entry.Requester.Get();
		if (!OtherRequester || IsRequesterReserved(OtherRequester))
		{
			continue;
		}

		if (Entry.Sequence < RequesterSequence)
		{
			++PendingBeforeRequester;
		}
	}

	OutPendingIndex = PendingBeforeRequester;
	return true;
}

bool UCombatEngagementSlotComponent::IsRequesterReserved(AActor* Requester) const
{
	int32 IgnoredIndex = INDEX_NONE;
	return Requester
		&& (FindReservation(AttackReservations, Requester, IgnoredIndex)
			|| FindReservation(WaitReservations, Requester, IgnoredIndex)
			|| FindReservation(HoldingReservations, Requester, IgnoredIndex));
}

bool UCombatEngagementSlotComponent::HasAnyValidReservation(
	const TArray<TWeakObjectPtr<AActor>>& Reservations) const
{
	for (const TWeakObjectPtr<AActor>& Reservation : Reservations)
	{
		if (Reservation.IsValid())
		{
			return true;
		}
	}
	return false;
}

void UCombatEngagementSlotComponent::RegisterQueueRequester(AActor* Requester)
{
	if (!Requester)
	{
		return;
	}

	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (Entry.Requester.Get() == Requester)
		{
			return;
		}
	}

	FEngagementQueueEntry& Entry = EngagementQueue.AddDefaulted_GetRef();
	Entry.Requester = Requester;
	Entry.Sequence = NextQueueSequence++;
	if (IsCorridorFormationActive())
	{
		AssignCorridorSideIndices(false);
	}
	if (const UWorld* World = GetWorld())
	{
		LastRequesterRegistrationTime = World->GetTimeSeconds();
	}
}

void UCombatEngagementSlotComponent::RemoveQueueRequester(AActor* Requester)
{
	for (int32 QueueIndex = EngagementQueue.Num() - 1; QueueIndex >= 0; --QueueIndex)
	{
		if (EngagementQueue[QueueIndex].Requester.Get() == Requester)
		{
			EngagementQueue.RemoveAtSwap(QueueIndex, 1, EAllowShrinking::No);
		}
	}
}

uint64 UCombatEngagementSlotComponent::GetQueueSequence(AActor* Requester) const
{
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (Entry.Requester.Get() == Requester)
		{
			return Entry.Sequence;
		}
	}
	return TNumericLimits<uint64>::Max();
}

float UCombatEngagementSlotComponent::GetAttackEligibleTime(AActor* Requester) const
{
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (Entry.Requester.Get() == Requester)
		{
			return Entry.AttackEligibleTime;
		}
	}
	return TNumericLimits<float>::Max();
}

void UCombatEngagementSlotComponent::SetAttackEligibleTime(AActor* Requester, float EligibleTime)
{
	for (FEngagementQueueEntry& Entry : EngagementQueue)
	{
		if (Entry.Requester.Get() == Requester)
		{
			Entry.AttackEligibleTime = FMath::Max(0.0f, EligibleTime);
			return;
		}
	}
}

bool UCombatEngagementSlotComponent::IsInitialFormationActive() const
{
	return bInitialFormationActive;
}

void UCombatEngagementSlotComponent::TryReformFormation()
{
	const AActor* Owner = GetOwner();
	if (!Owner || ReformTriggerDistance <= 0.0f)
	{
		return;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const float TriggerDistanceSquared = FMath::Square(ReformTriggerDistance);
	const bool bOwnerMovedEnough = FVector::DistSquared2D(
		LastReformOwnerLocation,
		OwnerLocation) >= TriggerDistanceSquared;
	const bool bAnchorMovedEnough = FVector::DistSquared2D(
		LastReformAnchorLocation,
		EngagementAnchorLocation) >= TriggerDistanceSquared;
	if (!bOwnerMovedEnough && !bAnchorMovedEnough)
	{
		return;
	}

	LastReformOwnerLocation = OwnerLocation;
	LastReformAnchorLocation = EngagementAnchorLocation;
	if (IsPocketFormationActive())
	{
		ActivatePocketRapidReform();
	}
	ReformReservations();
}

void UCombatEngagementSlotComponent::ReformReservations()
{
	if (IsCorridorFormationActive())
	{
		ReconcileCorridorAttackReservations();
		ReformRingReservations(AttackReservations, EBHCombatSlotType::Attack);
		ReformRingReservations(WaitReservations, EBHCombatSlotType::Wait);
		ReformRingReservations(HoldingReservations, EBHCombatSlotType::Holding);
		++FormationRevision;
		NotifyAllReservedRequestersSlotChanged();
		return;
	}

	ReformRingReservations(AttackReservations, EBHCombatSlotType::Attack);
	ReformRingReservations(WaitReservations, EBHCombatSlotType::Wait);
	ReformRingReservations(HoldingReservations, EBHCombatSlotType::Holding);
	++FormationRevision;
	NotifyAllReservedRequestersSlotChanged();

	UE_LOG(
		LogProjectBH,
		Display,
		TEXT("%s reformed engagement rings. Revision: %d"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
		FormationRevision);
}

void UCombatEngagementSlotComponent::ReformRingReservations(
	TArray<TWeakObjectPtr<AActor>>& Reservations,
	EBHCombatSlotType SlotType)
{
	TArray<FVector, TInlineAllocator<16>> SlotLocations;
	SlotLocations.SetNum(Reservations.Num());
	for (int32 SlotIndex = 0; SlotIndex < Reservations.Num(); ++SlotIndex)
	{
		if (!GetSlotWorldLocation(SlotType, SlotIndex, SlotLocations[SlotIndex]))
		{
			return;
		}
	}

	const auto IsLockedReservation = [SlotType](const AActor* Requester)
	{
		const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
		return SlotType == EBHCombatSlotType::Attack
			&& Enemy
			&& (Enemy->GetCombatState() == EBHEnemyCombatState::Attacking
				|| Enemy->GetCombatState() == EBHEnemyCombatState::Recovering
				|| Enemy->GetAttackSlotCost() > 1
				|| Enemy->GetAttackSlotExclusionRadius() > 0.0f);
	};

	// Preserve the occupied slot set and layer roles. Only exchange two owners
	// when the swap clears a meaningful amount of combined remaining travel.
	// This fixes visible crossovers without making the whole formation dance for
	// tiny distance differences on every player movement sample.
	const float MinimumSaving = FMath::Max(0.0f, ReformMinimumTravelSaving);
	int32 SwapCount = 0;
	for (int32 PassIndex = 0; PassIndex < Reservations.Num(); ++PassIndex)
	{
		int32 BestSlotA = INDEX_NONE;
		int32 BestSlotB = INDEX_NONE;
		float BestSaving = MinimumSaving;
		for (int32 SlotA = 0; SlotA < Reservations.Num(); ++SlotA)
		{
			AActor* RequesterA = Reservations[SlotA].Get();
			if (!RequesterA || IsLockedReservation(RequesterA))
			{
				continue;
			}
			for (int32 SlotB = SlotA + 1; SlotB < Reservations.Num(); ++SlotB)
			{
				AActor* RequesterB = Reservations[SlotB].Get();
				if (!RequesterB || IsLockedReservation(RequesterB))
				{
					continue;
				}
				if (IsCorridorFormationActive())
				{
					const TArray<FCorridorRowSlot>* CorridorLayout = SlotType == EBHCombatSlotType::Wait
						? &CorridorWaitRowLayout
						: (SlotType == EBHCombatSlotType::Holding ? &CorridorHoldingRowLayout : nullptr);
					const bool bRequesterACanUseSlotB = SlotType == EBHCombatSlotType::Attack
						? IsCorridorAttackSlotOnRequesterSide(RequesterA, SlotB)
						: (CorridorLayout
							&& CorridorLayout->IsValidIndex(SlotB)
							&& (*CorridorLayout)[SlotB].SideIndex == GetCorridorSideIndex(RequesterA));
					const bool bRequesterBCanUseSlotA = SlotType == EBHCombatSlotType::Attack
						? IsCorridorAttackSlotOnRequesterSide(RequesterB, SlotA)
						: (CorridorLayout
							&& CorridorLayout->IsValidIndex(SlotA)
							&& (*CorridorLayout)[SlotA].SideIndex == GetCorridorSideIndex(RequesterB));
					if (!bRequesterACanUseSlotB || !bRequesterBCanUseSlotA)
					{
						continue;
					}
				}

				float CurrentTravelA = FVector::Dist2D(
					RequesterA->GetActorLocation(), SlotLocations[SlotA]);
				float CurrentTravelB = FVector::Dist2D(
					RequesterB->GetActorLocation(), SlotLocations[SlotB]);
				float SwappedTravelA = FVector::Dist2D(
					RequesterA->GetActorLocation(), SlotLocations[SlotB]);
				float SwappedTravelB = FVector::Dist2D(
					RequesterB->GetActorLocation(), SlotLocations[SlotA]);
				if (IsPocketFormationActive() && SlotType == EBHCombatSlotType::Attack)
				{
					const bool bCurrentPathsValid = GetNavigationPathScore(
						RequesterA, SlotLocations[SlotA], CurrentTravelA)
						&& GetNavigationPathScore(
							RequesterB, SlotLocations[SlotB], CurrentTravelB);
					const bool bSwappedPathsValid = GetNavigationPathScore(
						RequesterA, SlotLocations[SlotB], SwappedTravelA)
						&& GetNavigationPathScore(
							RequesterB, SlotLocations[SlotA], SwappedTravelB);
					if (!bSwappedPathsValid)
					{
						continue;
					}
					if (!bCurrentPathsValid)
					{
						CurrentTravelA = TNumericLimits<float>::Max() * 0.25f;
						CurrentTravelB = TNumericLimits<float>::Max() * 0.25f;
					}
				}
				const float CurrentTravel = CurrentTravelA + CurrentTravelB;
				const float SwappedTravel = SwappedTravelA + SwappedTravelB;
				const float TravelSaving = CurrentTravel - SwappedTravel;
				if (TravelSaving >= MinimumSaving
					&& (BestSlotA == INDEX_NONE || TravelSaving > BestSaving))
				{
					BestSaving = TravelSaving;
					BestSlotA = SlotA;
					BestSlotB = SlotB;
				}
			}
		}

		if (BestSlotA == INDEX_NONE || BestSlotB == INDEX_NONE)
		{
			break;
		}
		Reservations.Swap(BestSlotA, BestSlotB);
		++SwapCount;
	}

	if (SwapCount > 0)
	{
		UE_LOG(
			LogProjectBH,
			VeryVerbose,
			TEXT("%s reformed %s reservations with %d travel-saving swaps."),
			GetOwner() ? *GetOwner()->GetName() : TEXT("InvalidSlotOwner"),
			*StaticEnum<EBHCombatSlotType>()->GetNameStringByValue(static_cast<int64>(SlotType)),
			SwapCount);
	}
}

int32 UCombatEngagementSlotComponent::GetOccupiedAttackSlotCost() const
{
	int32 TotalCost = 0;
	for (const TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		const ABHEnemy* Enemy = Cast<ABHEnemy>(Reservation.Get());
		if (Enemy)
		{
			TotalCost += Enemy->GetAttackSlotCost();
		}
		else if (Reservation.IsValid())
		{
			++TotalCost;
		}
	}
	return TotalCost;
}

bool UCombatEngagementSlotComponent::IsAttackSlotBlockedByCurrentOccupancy(
	int32 AttackSlotIndex) const
{
	if (!AttackReservations.IsValidIndex(AttackSlotIndex)
		|| AttackSlotIndex >= GetActiveAttackSlotCount()
		|| AttackReservations[AttackSlotIndex].IsValid())
	{
		return false;
	}

	if (GetOccupiedAttackSlotCost() >= GetActiveAttackSlotCount())
	{
		return true;
	}

	FVector CandidateLocation;
	if (!GetSlotWorldLocation(
		EBHCombatSlotType::Attack,
		AttackSlotIndex,
		CandidateLocation))
	{
		return true;
	}

	for (int32 OtherSlotIndex = 0; OtherSlotIndex < AttackReservations.Num(); ++OtherSlotIndex)
	{
		const ABHEnemy* OtherEnemy = Cast<ABHEnemy>(AttackReservations[OtherSlotIndex].Get());
		const float ExclusionRadius = OtherEnemy
			? OtherEnemy->GetAttackSlotExclusionRadius()
			: 0.0f;
		if (ExclusionRadius <= 0.0f)
		{
			continue;
		}

		FVector OtherSlotLocation;
		if (!GetSlotWorldLocation(
			EBHCombatSlotType::Attack,
			OtherSlotIndex,
			OtherSlotLocation)
			|| FVector::DistSquared2D(CandidateLocation, OtherSlotLocation)
				< FMath::Square(ExclusionRadius))
		{
			return true;
		}
	}

	return false;
}

bool UCombatEngagementSlotComponent::CanRequesterOccupyAttackSlot(
	AActor* Requester,
	int32 AttackSlotIndex,
	const AActor* ReplacedRequester) const
{
	const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
	if (!Enemy
		|| !AttackReservations.IsValidIndex(AttackSlotIndex)
		|| AttackSlotIndex >= GetActiveAttackSlotCount())
	{
		return false;
	}

	const AActor* ExistingOwner = AttackReservations[AttackSlotIndex].Get();
	if (ExistingOwner
		&& ExistingOwner != Requester
		&& ExistingOwner != ReplacedRequester)
	{
		return false;
	}

	int32 ExistingCost = 0;
	int32 SameSizeAttackerCount = 0;
	for (const TWeakObjectPtr<AActor>& Reservation : AttackReservations)
	{
		const AActor* OtherRequester = Reservation.Get();
		if (!OtherRequester
			|| OtherRequester == Requester
			|| OtherRequester == ReplacedRequester)
		{
			continue;
		}

		const ABHEnemy* OtherEnemy = Cast<ABHEnemy>(OtherRequester);
		ExistingCost += OtherEnemy ? OtherEnemy->GetAttackSlotCost() : 1;
		if (OtherEnemy && OtherEnemy->GetEnemySizeClass() == Enemy->GetEnemySizeClass())
		{
			++SameSizeAttackerCount;
		}
	}

	if (ExistingCost + Enemy->GetAttackSlotCost() > GetActiveAttackSlotCount())
	{
		return false;
	}

	const int32 SizeClassLimit = Enemy->GetMaxConcurrentAttackersOfSize();
	if (SizeClassLimit > 0 && SameSizeAttackerCount >= SizeClassLimit)
	{
		return false;
	}

	FVector CandidateLocation;
	if (!GetSlotWorldLocation(
		EBHCombatSlotType::Attack,
		AttackSlotIndex,
		CandidateLocation))
	{
		return false;
	}

	const float RequesterExclusionRadius = Enemy->GetAttackSlotExclusionRadius();
	for (int32 OtherSlotIndex = 0; OtherSlotIndex < AttackReservations.Num(); ++OtherSlotIndex)
	{
		const AActor* OtherRequester = AttackReservations[OtherSlotIndex].Get();
		if (!OtherRequester
			|| OtherRequester == Requester
			|| OtherRequester == ReplacedRequester)
		{
			continue;
		}

		const ABHEnemy* OtherEnemy = Cast<ABHEnemy>(OtherRequester);
		const float RequiredClearance = FMath::Max(
			RequesterExclusionRadius,
			OtherEnemy ? OtherEnemy->GetAttackSlotExclusionRadius() : 0.0f);
		if (RequiredClearance <= 0.0f)
		{
			continue;
		}

		FVector OtherSlotLocation;
		if (!GetSlotWorldLocation(
			EBHCombatSlotType::Attack,
			OtherSlotIndex,
			OtherSlotLocation)
			|| FVector::DistSquared2D(CandidateLocation, OtherSlotLocation)
				< FMath::Square(RequiredClearance))
		{
			return false;
		}
	}

	return true;
}

float UCombatEngagementSlotComponent::GetMaximumAttackSlotDistance(AActor* Requester) const
{
	const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
	if (!Enemy)
	{
		return 0.0f;
	}

	float ArrivalTolerance = 20.0f;
	if (const ABHCrowdEnemyAIController* CrowdController = Cast<ABHCrowdEnemyAIController>(Enemy->GetController()))
	{
		ArrivalTolerance = CrowdController->GetSlotAcceptanceRadius();
	}

	return FMath::Max(0.0f, Enemy->GetAttackStartRange() - FMath::Max(0.0f, ArrivalTolerance));
}

bool UCombatEngagementSlotComponent::TryReserveSlot(
	AActor* Requester,
	EBHCombatSlotType SlotType,
	float MaxDistanceFromOwner,
	int32 ExcludedSlotIndex)
{
	TArray<TWeakObjectPtr<AActor>>* Reservations = nullptr;
	switch (SlotType)
	{
	case EBHCombatSlotType::Attack:
		Reservations = &AttackReservations;
		break;
	case EBHCombatSlotType::Wait:
		Reservations = &WaitReservations;
		break;
	case EBHCombatSlotType::Holding:
		Reservations = &HoldingReservations;
		break;
	default:
		return false;
	}

	int32 BestSlotIndex = INDEX_NONE;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	float BestPathScore = TNumericLimits<float>::Max();
	bool bBestSlotNeedsStagedRoute = true;
	bool bBestUsesPreferredSide = false;
	bool bBestUsesPreferredChannel = false;
	const bool bUsesCorridorAttackPreference = IsCorridorFormationActive()
		&& SlotType == EBHCombatSlotType::Attack;
	const bool bUsesCorridorQueueOrder = IsCorridorFormationActive()
		&& SlotType != EBHCombatSlotType::Attack;
	const int32 RequesterSideIndex = bUsesCorridorAttackPreference
		? GetCorridorSideIndex(Requester)
		: INDEX_NONE;
	const int32 RequesterChannelIndex = bUsesCorridorAttackPreference
		? GetCorridorQueueChannelIndex(Requester)
		: INDEX_NONE;
	for (int32 SlotIndex = 0; SlotIndex < Reservations->Num(); ++SlotIndex)
	{
		if (SlotIndex == ExcludedSlotIndex)
		{
			continue;
		}

		if ((*Reservations)[SlotIndex].IsValid())
		{
			continue;
		}
		if (SlotType == EBHCombatSlotType::Attack
			&& !CanRequesterOccupyAttackSlot(Requester, SlotIndex))
		{
			continue;
		}
		FVector SlotLocation;
		if (!GetSlotWorldLocation(SlotType, SlotIndex, SlotLocation))
		{
			continue;
		}

		if (MaxDistanceFromOwner >= 0.0f
			&& FVector::DistSquared2D(GetOwner()->GetActorLocation(), SlotLocation)
				> FMath::Square(MaxDistanceFromOwner))
		{
			continue;
		}
		if (bUsesCorridorAttackPreference)
		{
			if (!IsCorridorAttackSlotOnRequesterSide(Requester, SlotIndex))
			{
				continue;
			}
			float PathScore = 0.0f;
			if (!GetNavigationPathScore(Requester, SlotLocation, PathScore))
			{
				continue;
			}

			const bool bUsesPreferredSide = GetCorridorAttackSlotSideIndex(SlotIndex)
				== RequesterSideIndex;
			const bool bUsesPreferredChannel = GetCorridorAttackSlotChannelIndex(SlotIndex)
				== RequesterChannelIndex;
			if (BestSlotIndex == INDEX_NONE
				|| (bUsesPreferredSide && !bBestUsesPreferredSide)
				|| (bUsesPreferredSide == bBestUsesPreferredSide
					&& bUsesPreferredChannel && !bBestUsesPreferredChannel)
				|| (bUsesPreferredSide == bBestUsesPreferredSide
					&& bUsesPreferredChannel == bBestUsesPreferredChannel
					&& PathScore < BestPathScore))
			{
				BestSlotIndex = SlotIndex;
				BestPathScore = PathScore;
				bBestUsesPreferredSide = bUsesPreferredSide;
				bBestUsesPreferredChannel = bUsesPreferredChannel;
			}
			continue;
		}
		if (bUsesCorridorQueueOrder)
		{
			const TArray<FCorridorRowSlot>* CorridorLayout = SlotType == EBHCombatSlotType::Wait
				? &CorridorWaitRowLayout
				: (SlotType == EBHCombatSlotType::Holding ? &CorridorHoldingRowLayout : nullptr);
			if (!CorridorLayout
				|| !CorridorLayout->IsValidIndex(SlotIndex)
				|| (*CorridorLayout)[SlotIndex].SideIndex != GetCorridorSideIndex(Requester))
			{
				continue;
			}
			// Queue order is dense only within the requester's physical Corridor side.
			BestSlotIndex = SlotIndex;
			break;
		}

		const float DistanceSquared = FVector::DistSquared2D(Requester->GetActorLocation(), SlotLocation);
		const bool bNeedsStagedRoute = DoesSegmentCrossCombatCore(Requester->GetActorLocation(), SlotLocation);
		const bool bPreferDirectSlot = bBestSlotNeedsStagedRoute && !bNeedsStagedRoute;
		if (BestSlotIndex == INDEX_NONE
			|| bPreferDirectSlot
			|| (bBestSlotNeedsStagedRoute == bNeedsStagedRoute && DistanceSquared < BestDistanceSquared))
		{
			BestDistanceSquared = DistanceSquared;
			BestSlotIndex = SlotIndex;
			bBestSlotNeedsStagedRoute = bNeedsStagedRoute;
		}
	}

	if (BestSlotIndex == INDEX_NONE)
	{
		return false;
	}

	(*Reservations)[BestSlotIndex] = Requester;
	if (SlotType == EBHCombatSlotType::Attack)
	{
		UpdateCorridorSideForAttackReservation(Requester, BestSlotIndex);
	}
	return true;
}

bool UCombatEngagementSlotComponent::FindReservation(
	const TArray<TWeakObjectPtr<AActor>>& Reservations,
	AActor* Requester,
	int32& OutSlotIndex) const
{
	OutSlotIndex = INDEX_NONE;
	for (int32 SlotIndex = 0; SlotIndex < Reservations.Num(); ++SlotIndex)
	{
		if (Reservations[SlotIndex].Get() == Requester)
		{
			OutSlotIndex = SlotIndex;
			return true;
		}
	}

	return false;
}

bool UCombatEngagementSlotComponent::GetSlotWorldLocation(
	EBHCombatSlotType SlotType,
	int32 SlotIndex,
	FVector& OutWorldLocation) const
{
	const AActor* Owner = GetOwner();
	int32 SlotCount = 0;
	float RingRadius = 0.0f;
	float AngleOffset = 0.0f;
	switch (SlotType)
	{
	case EBHCombatSlotType::Attack:
		SlotCount = AttackReservations.Num();
		RingRadius = AttackRingRadius;
		AngleOffset = AttackRingAngleOffset;
		break;
	case EBHCombatSlotType::Wait:
		SlotCount = WaitReservations.Num();
		RingRadius = WaitRingRadius;
		AngleOffset = WaitRingAngleOffset;
		break;
	case EBHCombatSlotType::Holding:
		SlotCount = HoldingReservations.Num();
		RingRadius = HoldingRingRadius;
		AngleOffset = HoldingRingAngleOffset;
		break;
	default:
		return false;
	}
	if (!Owner || SlotType == EBHCombatSlotType::None || !FMath::IsWithin(SlotIndex, 0, SlotCount))
	{
		return false;
	}
	if (IsCorridorFormationActive())
	{
		return GetCorridorSlotWorldLocation(SlotType, SlotIndex, OutWorldLocation);
	}
	if (IsPocketFormationActive())
	{
		return GetPocketSlotWorldLocation(SlotType, SlotIndex, OutWorldLocation);
	}

	const float AngleDegrees = AngleOffset + (360.0f * static_cast<float>(SlotIndex) / static_cast<float>(SlotCount));
	const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
	const FVector RingDirection(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0f);
	const FVector RingCenter = SlotType == EBHCombatSlotType::Attack
		? Owner->GetActorLocation()
		: EngagementAnchorLocation;
	const FVector DesiredLocation = RingCenter + RingDirection * RingRadius;

	return ProjectToNavigation(DesiredLocation, OutWorldLocation);
}

bool UCombatEngagementSlotComponent::DoesSegmentCrossCombatCore(
	const FVector& SegmentStart,
	const FVector& SegmentEnd) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	const FVector2D Center(Owner->GetActorLocation());
	const float EndRadius = FVector2D::Distance(Center, FVector2D(SegmentEnd));
	const float EffectiveRadius = FMath::Min(
		GetEffectiveCombatCoreRadius(),
		FMath::Max(0.0f, EndRadius - 5.0f));
	return DoesSegmentCrossCombatRadius(SegmentStart, SegmentEnd, EffectiveRadius);
}

bool UCombatEngagementSlotComponent::DoesSegmentCrossCombatRadius(
	const FVector& SegmentStart,
	const FVector& SegmentEnd,
	float Radius) const
{
	const AActor* Owner = GetOwner();
	const float EffectiveRadius = FMath::Max(0.0f, Radius);
	if (!Owner || EffectiveRadius <= 0.0f)
	{
		return false;
	}

	const FVector2D Center(Owner->GetActorLocation());
	const FVector2D Start(SegmentStart);
	const FVector2D End(SegmentEnd);
	const FVector2D Segment = End - Start;
	const float SegmentLengthSquared = Segment.SizeSquared();
	if (SegmentLengthSquared <= UE_KINDA_SMALL_NUMBER)
	{
		return FVector2D::DistSquared(Start, Center) < FMath::Square(EffectiveRadius);
	}

	const float ClosestAlpha = FMath::Clamp(
		FVector2D::DotProduct(Center - Start, Segment) / SegmentLengthSquared,
		0.0f,
		1.0f);
	const FVector2D ClosestPoint = Start + Segment * ClosestAlpha;
	return FVector2D::DistSquared(ClosestPoint, Center) < FMath::Square(EffectiveRadius);
}

float UCombatEngagementSlotComponent::GetEffectiveCombatCoreRadius() const
{
	return FMath::Min(
		FMath::Max(0.0f, CombatCoreRadius),
		FMath::Max(0.0f, AttackRingRadius - 5.0f));
}

bool UCombatEngagementSlotComponent::ShouldUseCombatCoreEscape(
	AActor* Requester,
	EBHCombatMoveRouteStage PreviousRouteStage) const
{
	const AActor* Owner = GetOwner();
	if (!bEnableCombatCoreEscape || !Requester || !Owner)
	{
		return false;
	}

	const float RequesterRadius = FVector::Dist2D(
		Requester->GetActorLocation(),
		Owner->GetActorLocation());
	if (RequesterRadius < GetEffectiveCombatCoreRadius())
	{
		return true;
	}

	const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
	const ABHCrowdEnemyAIController* Controller = Enemy
		? Cast<ABHCrowdEnemyAIController>(Enemy->GetController())
		: nullptr;
	const bool bWasEscaping = PreviousRouteStage == EBHCombatMoveRouteStage::CoreEscape
		|| (Controller && Controller->IsEscapingCombatCore());
	return bWasEscaping
		&& RequesterRadius < FMath::Max(
			GetEffectiveCombatCoreRadius() + 1.0f,
			CombatCoreEscapeExitRadius);
}

bool UCombatEngagementSlotComponent::CanUseDirectAttackCoreExit(
	AActor* Requester,
	const FVector& AttackSlotLocation) const
{
	const AActor* Owner = GetOwner();
	if (!Requester || !Owner)
	{
		return false;
	}

	FVector RequesterDirection = Requester->GetActorLocation() - Owner->GetActorLocation();
	RequesterDirection.Z = 0.0f;
	FVector SlotDirection = AttackSlotLocation - Owner->GetActorLocation();
	SlotDirection.Z = 0.0f;
	const float RequesterRadius = RequesterDirection.Size2D();
	const float SlotRadius = SlotDirection.Size2D();
	if (RequesterRadius >= GetEffectiveCombatCoreRadius()
		|| SlotRadius <= RequesterRadius + 5.0f
		|| RequesterRadius <= UE_KINDA_SMALL_NUMBER
		|| SlotRadius <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	return FVector::DotProduct(
		RequesterDirection / RequesterRadius,
		SlotDirection / SlotRadius) >= 0.5f;
}

bool UCombatEngagementSlotComponent::ResolveCombatCoreEscapeGoal(
	AActor* Requester,
	FVector& OutMoveGoal,
	EBHCombatMoveRouteStage& OutRouteStage) const
{
	const AActor* Owner = GetOwner();
	if (!Requester || !Owner)
	{
		return false;
	}

	const FVector RouteCenter = Owner->GetActorLocation();
	const FVector RequesterLocation = Requester->GetActorLocation();
	FVector InitialDirection = RequesterLocation - RouteCenter;
	InitialDirection.Z = 0.0f;
	if (InitialDirection.IsNearlyZero())
	{
		InitialDirection = -Owner->GetActorForwardVector().GetSafeNormal2D();
	}
	else
	{
		InitialDirection.Normalize();
	}

	const float EscapeRadius = FMath::Max3(
		GetEffectiveCombatCoreRadius() + 1.0f,
		AttackRingRadius + FMath::Max(1.0f, CorridorAgentRadius),
		CombatCoreEscapeExitRadius);
	const int32 CandidateCount = FMath::Clamp(CombatCoreEscapeCandidateCount, 4, 32);
	const float ProjectionToleranceSquared = FMath::Square(
		FMath::Max(0.0f, CombatCoreBypassProjectionTolerance));
	const float OccupiedPathClearanceSquared = FMath::Square(
		FMath::Max(0.0f, CombatCoreEscapeOccupiedPathClearance));

	const auto PointSegmentDistanceSquared2D = [](
		const FVector& Point,
		const FVector& SegmentStart,
		const FVector& SegmentEnd)
	{
		const FVector2D Point2D(Point);
		const FVector2D Start2D(SegmentStart);
		const FVector2D End2D(SegmentEnd);
		const FVector2D Segment = End2D - Start2D;
		const float SegmentLengthSquared = Segment.SizeSquared();
		if (SegmentLengthSquared <= UE_KINDA_SMALL_NUMBER)
		{
			return FVector2D::DistSquared(Point2D, Start2D);
		}
		const float Alpha = FMath::Clamp(
			FVector2D::DotProduct(Point2D - Start2D, Segment) / SegmentLengthSquared,
			0.0f,
			1.0f);
		return FVector2D::DistSquared(Point2D, Start2D + Segment * Alpha);
	};

	bool bFoundCandidate = false;
	int32 BestBlockingAttackCount = TNumericLimits<int32>::Max();
	float BestScore = TNumericLimits<float>::Max();
	for (int32 CandidateIndex = 0; CandidateIndex < CandidateCount; ++CandidateIndex)
	{
		const float AngleDegrees = 360.0f
			* static_cast<float>(CandidateIndex) / static_cast<float>(CandidateCount);
		const FVector CandidateDirection = InitialDirection.RotateAngleAxis(
			AngleDegrees,
			FVector::UpVector);
		const FVector DesiredLocation = RouteCenter + CandidateDirection * EscapeRadius;
		FVector ProjectedLocation;
		if (!ProjectToNavigation(DesiredLocation, ProjectedLocation)
			|| FVector::DistSquared2D(DesiredLocation, ProjectedLocation)
				> ProjectionToleranceSquared
			|| !HasCorridorAttackCandidateClearance(ProjectedLocation))
		{
			continue;
		}

		float PathScore = 0.0f;
		if (!GetNavigationPathScoreBetween(
				Requester,
				RequesterLocation,
				ProjectedLocation,
				PathScore))
		{
			continue;
		}

		int32 BlockingAttackCount = 0;
		for (const TWeakObjectPtr<AActor>& Reservation : AttackReservations)
		{
			const AActor* AttackOwner = Reservation.Get();
			if (AttackOwner
				&& AttackOwner != Requester
				&& PointSegmentDistanceSquared2D(
					AttackOwner->GetActorLocation(),
					RequesterLocation,
					ProjectedLocation) < OccupiedPathClearanceSquared)
			{
				++BlockingAttackCount;
			}
		}

		const float AngularDeviation = FMath::Min(AngleDegrees, 360.0f - AngleDegrees);
		const float CandidateScore = PathScore + AngularDeviation;
		if (!bFoundCandidate
			|| BlockingAttackCount < BestBlockingAttackCount
			|| (BlockingAttackCount == BestBlockingAttackCount
				&& CandidateScore < BestScore))
		{
			bFoundCandidate = true;
			BestBlockingAttackCount = BlockingAttackCount;
			BestScore = CandidateScore;
			OutMoveGoal = ProjectedLocation;
		}
	}

	if (!bFoundCandidate)
	{
		return false;
	}
	OutRouteStage = EBHCombatMoveRouteStage::CoreEscape;
	return true;
}

bool UCombatEngagementSlotComponent::HasActiveCombatCoreEscape() const
{
	const AActor* Owner = GetOwner();
	if (!bEnableCombatCoreEscape || !Owner)
	{
		return false;
	}

	const float EnterRadiusSquared = FMath::Square(GetEffectiveCombatCoreRadius());
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* Requester = Entry.Requester.Get();
		if (!Requester)
		{
			continue;
		}
		if (FVector::DistSquared2D(Requester->GetActorLocation(), Owner->GetActorLocation())
			< EnterRadiusSquared)
		{
			return true;
		}

		const ABHEnemy* Enemy = Cast<ABHEnemy>(Requester);
		const ABHCrowdEnemyAIController* Controller = Enemy
			? Cast<ABHCrowdEnemyAIController>(Enemy->GetController())
			: nullptr;
		if (Controller && Controller->IsEscapingCombatCore())
		{
			return true;
		}
	}
	return false;
}

bool UCombatEngagementSlotComponent::ResolveCorridorCombatCoreBypassGoal(
	AActor* Requester,
	const FVector& FinalSlotLocation,
	EBHCombatMoveRouteStage PreviousRouteStage,
	FVector& OutMoveGoal,
	EBHCombatMoveRouteStage& OutRouteStage) const
{
	const AActor* Owner = GetOwner();
	if (!Requester || !Owner)
	{
		return false;
	}

	const FVector RouteCenter = Owner->GetActorLocation();
	const FVector RequesterLocation = Requester->GetActorLocation();
	FVector RequesterDirection = RequesterLocation - RouteCenter;
	RequesterDirection.Z = 0.0f;
	FVector TargetDirection = FinalSlotLocation - RouteCenter;
	TargetDirection.Z = 0.0f;
	const float RequesterRadius = RequesterDirection.Size2D();
	const float TargetRadius = TargetDirection.Size2D();
	if (TargetRadius <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	TargetDirection /= TargetRadius;
	if (RequesterRadius <= UE_KINDA_SMALL_NUMBER)
	{
		RequesterDirection = -TargetDirection;
	}
	else
	{
		RequesterDirection /= RequesterRadius;
	}

	const float BypassRadius = FMath::Max3(
		GetEffectiveCombatCoreRadius() + FMath::Max(0.0f, CombatCoreBypassPadding),
		AttackRingRadius + FMath::Max(1.0f, OrbitRingAcceptanceRadius),
		1.0f);
	const float ProjectionToleranceSquared = FMath::Square(
		FMath::Max(0.0f, CombatCoreBypassProjectionTolerance));
	if (FMath::Abs(RequesterRadius - BypassRadius) > OrbitRingAcceptanceRadius)
	{
		const FVector DesiredApproach = RouteCenter + RequesterDirection * BypassRadius;
		FVector ProjectedApproach;
		float ApproachPathScore = 0.0f;
		if (ProjectToNavigation(DesiredApproach, ProjectedApproach)
			&& FVector::DistSquared2D(DesiredApproach, ProjectedApproach)
				<= ProjectionToleranceSquared
			&& GetNavigationPathScoreBetween(
				Requester,
				RequesterLocation,
				ProjectedApproach,
				ApproachPathScore,
				RequesterRadius >= GetEffectiveCombatCoreRadius()))
		{
			OutMoveGoal = ProjectedApproach;
			OutRouteStage = EBHCombatMoveRouteStage::ApproachRing;
			return true;
		}
	}

	const float RequesterAngle = FMath::RadiansToDegrees(FMath::Atan2(
		RequesterDirection.Y,
		RequesterDirection.X));
	const float TargetAngle = FMath::RadiansToDegrees(FMath::Atan2(
		TargetDirection.Y,
		TargetDirection.X));
	const float MaximumStepAngle = FMath::Clamp(OrbitWaypointAngleStep, 5.0f, 90.0f);
	const float PositiveRemainingAngle = FMath::Fmod(
		TargetAngle - RequesterAngle + 360.0f,
		360.0f);
	const float NegativeRemainingAngle = FMath::Fmod(
		RequesterAngle - TargetAngle + 360.0f,
		360.0f);

	struct FBypassCandidate
	{
		FVector Location = FVector::ZeroVector;
		EBHCombatMoveRouteStage Stage = EBHCombatMoveRouteStage::Direct;
		float Score = TNumericLimits<float>::Max();
	};
	const auto BuildCandidate = [&](int32 RotationSign, FBypassCandidate& OutCandidate)
	{
		const float RemainingAngle = RotationSign > 0
			? PositiveRemainingAngle
			: NegativeRemainingAngle;
		const float StepAngle = RemainingAngle > RingIngressAngleTolerance
			? FMath::Min(MaximumStepAngle, RemainingAngle)
			: MaximumStepAngle;
		const float CandidateAngle = RequesterAngle
			+ static_cast<float>(RotationSign) * StepAngle;
		const float CandidateAngleRadians = FMath::DegreesToRadians(CandidateAngle);
		const FVector DesiredWaypoint = RouteCenter
			+ FVector(
				FMath::Cos(CandidateAngleRadians),
				FMath::Sin(CandidateAngleRadians),
				0.0f) * BypassRadius;
		FVector ProjectedWaypoint;
		if (!ProjectToNavigation(DesiredWaypoint, ProjectedWaypoint)
			|| FVector::DistSquared2D(DesiredWaypoint, ProjectedWaypoint)
				> ProjectionToleranceSquared
			|| DoesSegmentCrossCombatCore(RequesterLocation, ProjectedWaypoint))
		{
			return false;
		}

		float ApproachScore = 0.0f;
		float ExitScore = 0.0f;
		if (!GetNavigationPathScoreBetween(
				Requester,
				RequesterLocation,
				ProjectedWaypoint,
				ApproachScore,
				true)
			|| !GetNavigationPathScoreBetween(
				Requester,
				ProjectedWaypoint,
				FinalSlotLocation,
				ExitScore))
		{
			return false;
		}

		const float RemainingArcAngle = FMath::Max(0.0f, RemainingAngle - StepAngle);
		OutCandidate.Location = ProjectedWaypoint;
		OutCandidate.Stage = RotationSign > 0
			? EBHCombatMoveRouteStage::BypassCorePositive
			: EBHCombatMoveRouteStage::BypassCoreNegative;
		OutCandidate.Score = ApproachScore
			+ ExitScore
			+ BypassRadius * FMath::DegreesToRadians(RemainingArcAngle);
		return true;
	};

	const int32 LockedRotationSign = PreviousRouteStage == EBHCombatMoveRouteStage::BypassCorePositive
		? 1
		: (PreviousRouteStage == EBHCombatMoveRouteStage::BypassCoreNegative ? -1 : 0);
	FBypassCandidate BestCandidate;
	if (LockedRotationSign != 0 && BuildCandidate(LockedRotationSign, BestCandidate))
	{
		OutMoveGoal = BestCandidate.Location;
		OutRouteStage = BestCandidate.Stage;
		return true;
	}

	FBypassCandidate PositiveCandidate;
	FBypassCandidate NegativeCandidate;
	const bool bHasPositiveCandidate = BuildCandidate(1, PositiveCandidate);
	const bool bHasNegativeCandidate = BuildCandidate(-1, NegativeCandidate);
	if (!bHasPositiveCandidate && !bHasNegativeCandidate)
	{
		return false;
	}

	if (!bHasNegativeCandidate)
	{
		BestCandidate = PositiveCandidate;
	}
	else if (!bHasPositiveCandidate)
	{
		BestCandidate = NegativeCandidate;
	}
	else if (PositiveCandidate.Score + 1.0f < NegativeCandidate.Score)
	{
		BestCandidate = PositiveCandidate;
	}
	else if (NegativeCandidate.Score + 1.0f < PositiveCandidate.Score)
	{
		BestCandidate = NegativeCandidate;
	}
	else
	{
		const uint64 Sequence = GetQueueSequence(Requester);
		BestCandidate = Sequence % 2 == 0 ? PositiveCandidate : NegativeCandidate;
	}

	OutMoveGoal = BestCandidate.Location;
	OutRouteStage = BestCandidate.Stage;
	return true;
}

bool UCombatEngagementSlotComponent::ProjectToNavigation(const FVector& DesiredLocation, FVector& OutProjectedLocation) const
{
	const UWorld* World = GetWorld();
	const UNavigationSystemV1* NavigationSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavigationSystem)
	{
		return false;
	}

	FNavLocation ProjectedLocation;
	if (!NavigationSystem->ProjectPointToNavigation(DesiredLocation, ProjectedLocation, NavProjectionExtent))
	{
		return false;
	}

	OutProjectedLocation = ProjectedLocation.Location;
	return true;
}

bool UCombatEngagementSlotComponent::GetNavigationPathScore(
	AActor* Requester,
	const FVector& Destination,
	float& OutPathScore) const
{
	return GetNavigationPathScoreBetween(
		Requester,
		Requester ? Requester->GetActorLocation() : FVector::ZeroVector,
		Destination,
		OutPathScore);
}

bool UCombatEngagementSlotComponent::GetNavigationPathScoreBetween(
	AActor* Requester,
	const FVector& Start,
	const FVector& Destination,
	float& OutPathScore,
	bool bRejectCombatCoreCrossing) const
{
	OutPathScore = 0.0f;
	if (!Requester || !GetWorld())
	{
		return false;
	}

	UNavigationPath* NavigationPath = UNavigationSystemV1::FindPathToLocationSynchronously(
		GetWorld(),
		Start,
		Destination,
		Requester);
	if (!NavigationPath || !NavigationPath->IsValid() || NavigationPath->IsPartial())
	{
		return false;
	}
	if (bRejectCombatCoreCrossing)
	{
		for (int32 PointIndex = 1; PointIndex < NavigationPath->PathPoints.Num(); ++PointIndex)
		{
			if (DoesSegmentCrossCombatRadius(
				NavigationPath->PathPoints[PointIndex - 1],
				NavigationPath->PathPoints[PointIndex],
				GetEffectiveCombatCoreRadius()))
			{
				return false;
			}
		}
	}

	const float PathLength = NavigationPath->GetPathLength();
	if (PathLength < 0.0f)
	{
		return false;
	}

	OutPathScore = PathLength + CalculatePathCongestionPenalty(Requester, NavigationPath->PathPoints);
	return true;
}

float UCombatEngagementSlotComponent::CalculatePathCongestionPenalty(
	AActor* Requester,
	const TArray<FVector>& PathPoints) const
{
	if (!Requester || PathPoints.Num() < 2
		|| AdmissionCongestionRadius <= 0.0f
		|| AdmissionCongestionPenaltyPerAgent <= 0.0f)
	{
		return 0.0f;
	}

	const float CongestionRadiusSquared = FMath::Square(AdmissionCongestionRadius);
	int32 NearbyAgentCount = 0;
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* OtherRequester = Entry.Requester.Get();
		if (!OtherRequester || OtherRequester == Requester)
		{
			continue;
		}
		const ABHEnemy* RequesterEnemy = Cast<ABHEnemy>(Requester);
		const ABHEnemy* OtherEnemy = Cast<ABHEnemy>(OtherRequester);
		const UCapsuleComponent* RequesterCapsule = RequesterEnemy
			? RequesterEnemy->GetCapsuleComponent()
			: nullptr;
		const UCapsuleComponent* OtherCapsule = OtherEnemy
			? OtherEnemy->GetCapsuleComponent()
			: nullptr;
		const float SameLayerTolerance = RequesterCapsule && OtherCapsule
			? FMath::Max(
				RequesterCapsule->GetScaledCapsuleHalfHeight(),
				OtherCapsule->GetScaledCapsuleHalfHeight())
			: NavProjectionExtent.Z;
		if (FMath::Abs(Requester->GetActorLocation().Z - OtherRequester->GetActorLocation().Z)
			> SameLayerTolerance)
		{
			continue;
		}

		const FVector2D OtherLocation(OtherRequester->GetActorLocation());
		bool bNearPath = false;
		for (int32 PointIndex = 1; PointIndex < PathPoints.Num(); ++PointIndex)
		{
			const FVector2D SegmentStart(PathPoints[PointIndex - 1]);
			const FVector2D SegmentEnd(PathPoints[PointIndex]);
			const FVector2D Segment = SegmentEnd - SegmentStart;
			const float SegmentLengthSquared = Segment.SizeSquared();
			const float ClosestAlpha = SegmentLengthSquared <= UE_KINDA_SMALL_NUMBER
				? 0.0f
				: FMath::Clamp(
					FVector2D::DotProduct(OtherLocation - SegmentStart, Segment) / SegmentLengthSquared,
					0.0f,
					1.0f);
			const FVector2D ClosestPoint = SegmentStart + Segment * ClosestAlpha;
			if (FVector2D::DistSquared(OtherLocation, ClosestPoint) <= CongestionRadiusSquared)
			{
				bNearPath = true;
				break;
			}
		}

		if (bNearPath)
		{
			++NearbyAgentCount;
		}
	}

	return static_cast<float>(NearbyAgentCount) * AdmissionCongestionPenaltyPerAgent;
}

void UCombatEngagementSlotComponent::UpdateDebugMetrics()
{
	CurrentSpacingViolationCount = 0;
	CurrentAttackingEnemyCount = 0;
	CurrentNonAttackSlotAttackerCount = 0;

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	TArray<ABHEnemy*, TInlineAllocator<16>> EngagedEnemies;
	for (TActorIterator<ABHEnemy> It(World); It; ++It)
	{
		ABHEnemy* Enemy = *It;
		const ABHCrowdEnemyAIController* Controller = Enemy
			? Cast<ABHCrowdEnemyAIController>(Enemy->GetController())
			: nullptr;
		if (!Controller || Controller->GetCurrentTarget() != Owner)
		{
			continue;
		}

		EngagedEnemies.Add(Enemy);
		if (Enemy->GetCombatState() == EBHEnemyCombatState::Attacking)
		{
			++CurrentAttackingEnemyCount;
			if (Controller->GetCurrentCombatSlotType() != EBHCombatSlotType::Attack)
			{
				++CurrentNonAttackSlotAttackerCount;
			}
		}
	}

	for (int32 FirstIndex = 0; FirstIndex < EngagedEnemies.Num(); ++FirstIndex)
	{
		const ABHEnemy* FirstEnemy = EngagedEnemies[FirstIndex];
		const UCapsuleComponent* FirstCapsule = FirstEnemy ? FirstEnemy->GetCapsuleComponent() : nullptr;
		if (!FirstCapsule)
		{
			continue;
		}

		for (int32 SecondIndex = FirstIndex + 1; SecondIndex < EngagedEnemies.Num(); ++SecondIndex)
		{
			const ABHEnemy* SecondEnemy = EngagedEnemies[SecondIndex];
			const UCapsuleComponent* SecondCapsule = SecondEnemy ? SecondEnemy->GetCapsuleComponent() : nullptr;
			if (!SecondCapsule)
			{
				continue;
			}

			const FVector FirstLocation = FirstEnemy->GetActorLocation();
			const FVector SecondLocation = SecondEnemy->GetActorLocation();
			const float SameLayerTolerance = FMath::Max(
				FirstCapsule->GetScaledCapsuleHalfHeight(),
				SecondCapsule->GetScaledCapsuleHalfHeight());
			if (FMath::Abs(FirstLocation.Z - SecondLocation.Z) > SameLayerTolerance)
			{
				continue;
			}

			const float MinimumSpacing = FirstCapsule->GetScaledCapsuleRadius()
				+ SecondCapsule->GetScaledCapsuleRadius()
				- 5.0f;
			if (FVector::DistSquared2D(FirstLocation, SecondLocation) < FMath::Square(MinimumSpacing))
			{
				++CurrentSpacingViolationCount;
				DrawDebugLine(World, FirstLocation, SecondLocation, FColor::Magenta, false, 0.12f, 0, 3.0f);
			}
		}
	}

	PeakSpacingViolationCount = FMath::Max(PeakSpacingViolationCount, CurrentSpacingViolationCount);
}

void UCombatEngagementSlotComponent::DrawDebugSlots() const
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	DrawDebugCircle(
		World,
		Owner->GetActorLocation(),
		GetEffectiveCombatCoreRadius(),
		48,
		FColor::Orange,
		false,
		0.12f,
		0,
		2.0f,
		FVector::ForwardVector,
		FVector::RightVector,
		false);
	if (IsCorridorFormationActive())
	{
		const float BypassRadius = FMath::Max3(
			GetEffectiveCombatCoreRadius() + FMath::Max(0.0f, CombatCoreBypassPadding),
			AttackRingRadius + FMath::Max(1.0f, OrbitRingAcceptanceRadius),
			1.0f);
		DrawDebugCircle(
			World,
			Owner->GetActorLocation() + FVector(0.0f, 0.0f, 3.0f),
			BypassRadius,
			48,
			FColor::Purple,
			false,
			0.12f,
			0,
			1.0f,
			FVector::ForwardVector,
			FVector::RightVector,
			false);
	}

	const FColor AnchorDebugColor = bEngagementAnchorRecentering ? FColor::Yellow : FColor::Cyan;
	DrawDebugSphere(
		World,
		EngagementAnchorLocation,
		18.0f,
		10,
		AnchorDebugColor,
		false,
		0.12f,
		0,
		2.0f);
	DrawDebugLine(
		World,
		Owner->GetActorLocation(),
		EngagementAnchorLocation,
		AnchorDebugColor,
		false,
		0.12f,
		0,
		1.5f);

	DrawCombatSpaceAnalysisDebug();
	if (IsCorridorFormationActive())
	{
		for (int32 ProbeIndex = 0; ProbeIndex < CorridorAttackProbeLocations.Num(); ++ProbeIndex)
		{
			const bool bSelected = CorridorAttackProbeSelected.IsValidIndex(ProbeIndex)
				&& CorridorAttackProbeSelected[ProbeIndex] != 0;
			if (bSelected)
			{
				continue;
			}

			const bool bValid = CorridorAttackProbeValid.IsValidIndex(ProbeIndex)
				&& CorridorAttackProbeValid[ProbeIndex] != 0;
			DrawDebugSphere(
				World,
				CorridorAttackProbeLocations[ProbeIndex] + FVector(0.0f, 0.0f, 5.0f),
				bValid ? 10.0f : 7.0f,
				8,
				bValid ? FColor::White : FColor(110, 30, 30),
				false,
				0.12f,
				0,
				bValid ? 1.5f : 1.0f);
		}
	}

	const int32 ActiveAttackSlotCount = GetActiveAttackSlotCount();
	TArray<uint8> CorridorSideHeldAttackSlots;
	CorridorSideHeldAttackSlots.SetNumZeroed(ActiveAttackSlotCount);
	if (IsCorridorFormationActive())
	{
		for (int32 SlotIndex = 0; SlotIndex < ActiveAttackSlotCount; ++SlotIndex)
		{
			if (AttackReservations[SlotIndex].IsValid()
				|| IsAttackSlotBlockedByCurrentOccupancy(SlotIndex))
			{
				continue;
			}

			int32 SameSideWaitSlotIndex = INDEX_NONE;
			CorridorSideHeldAttackSlots[SlotIndex] =
				FindCorridorWaitAdmissionForAttackSlot(
					SlotIndex,
					SameSideWaitSlotIndex,
					false)
				? 0
				: 1;
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < ActiveAttackSlotCount; ++SlotIndex)
	{
		FVector SlotLocation;
		if (GetSlotWorldLocation(EBHCombatSlotType::Attack, SlotIndex, SlotLocation))
		{
			const bool bOccupied = AttackReservations[SlotIndex].IsValid();
			const bool bBlockedByOccupancy = !bOccupied
				&& IsAttackSlotBlockedByCurrentOccupancy(SlotIndex);
			const bool bHeldForSameSideCandidate = !bOccupied
				&& !bBlockedByOccupancy
				&& CorridorSideHeldAttackSlots.IsValidIndex(SlotIndex)
				&& CorridorSideHeldAttackSlots[SlotIndex] != 0;
			const bool bVacancyFallbackReady = !bOccupied
				&& !bBlockedByOccupancy
				&& !bHeldForSameSideCandidate
				&& AttackSlotVacantElapsed.IsValidIndex(SlotIndex)
				&& AttackSlotVacantElapsed[SlotIndex]
					>= FMath::Max(0.0f, AttackVacancyFallbackDelay);
			const FColor Color = bOccupied
				? FColor::Red
				: (bBlockedByOccupancy
					? FColor(100, 35, 120)
					: (bHeldForSameSideCandidate
						? FColor(50, 110, 255)
						: (bVacancyFallbackReady ? FColor::Orange : FColor::Green)));
			DrawDebugSphere(World, SlotLocation, 22.0f, 12, Color, false, 0.12f, 0, 2.0f);
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < WaitReservations.Num(); ++SlotIndex)
	{
		FVector SlotLocation;
		if (GetSlotWorldLocation(EBHCombatSlotType::Wait, SlotIndex, SlotLocation))
		{
			const FColor Color = WaitReservations[SlotIndex].IsValid() ? FColor::Yellow : FColor::Cyan;
			DrawDebugSphere(World, SlotLocation, 16.0f, 10, Color, false, 0.12f, 0, 1.5f);
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < HoldingReservations.Num(); ++SlotIndex)
	{
		FVector SlotLocation;
		if (GetSlotWorldLocation(EBHCombatSlotType::Holding, SlotIndex, SlotLocation))
		{
			const FColor Color = HoldingReservations[SlotIndex].IsValid()
				? FColor::Purple
				: FColor::Blue;
			DrawDebugSphere(World, SlotLocation, 13.0f, 8, Color, false, 0.12f, 0, 1.25f);
		}
	}

	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* Requester = Entry.Requester.Get();
		if (!Requester || IsRequesterReserved(Requester))
		{
			continue;
		}

		int32 PendingIndex = INDEX_NONE;
		FVector PendingLocation;
		if (GetPendingWaitLocation(Requester, PendingIndex, PendingLocation))
		{
			DrawDebugSphere(
				World,
				PendingLocation,
				10.0f,
				8,
				FColor(0, 200, 120),
				false,
				0.12f,
				0,
				1.0f);
		}
	}

	int32 OccupiedAttackSlots = 0;
	int32 BlockedAttackSlots = 0;
	int32 SideHeldAttackSlots = 0;
	float MaximumAttackVacancy = 0.0f;
	for (int32 SlotIndex = 0; SlotIndex < ActiveAttackSlotCount; ++SlotIndex)
	{
		const bool bOccupied = AttackReservations[SlotIndex].IsValid();
		const bool bBlockedByOccupancy = !bOccupied
			&& IsAttackSlotBlockedByCurrentOccupancy(SlotIndex);
		const bool bHeldForSameSideCandidate = !bOccupied
			&& !bBlockedByOccupancy
			&& CorridorSideHeldAttackSlots.IsValidIndex(SlotIndex)
			&& CorridorSideHeldAttackSlots[SlotIndex] != 0;
		OccupiedAttackSlots += bOccupied ? 1 : 0;
		BlockedAttackSlots += bBlockedByOccupancy ? 1 : 0;
		SideHeldAttackSlots += bHeldForSameSideCandidate ? 1 : 0;
		if (!bOccupied
			&& !bBlockedByOccupancy
			&& !bHeldForSameSideCandidate
			&& AttackSlotVacantElapsed.IsValidIndex(SlotIndex))
		{
			MaximumAttackVacancy = FMath::Max(
				MaximumAttackVacancy,
				AttackSlotVacantElapsed[SlotIndex]);
		}
	}

	int32 OccupiedWaitSlots = 0;
	for (const TWeakObjectPtr<AActor>& Reservation : WaitReservations)
	{
		OccupiedWaitSlots += Reservation.IsValid() ? 1 : 0;
	}

	int32 OccupiedHoldingSlots = 0;
	for (const TWeakObjectPtr<AActor>& Reservation : HoldingReservations)
	{
		OccupiedHoldingSlots += Reservation.IsValid() ? 1 : 0;
	}

	int32 PendingRequesterCount = 0;
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* Requester = Entry.Requester.Get();
		PendingRequesterCount += Requester && !IsRequesterReserved(Requester) ? 1 : 0;
	}

	const FString DebugText = FString::Printf(
		TEXT("Slots A:%d/%d Cost:%d/%d Blocked:%d SideHold:%d W:%d/%d H:%d/%d Q:%d | Phase:%s | Reform:%d | Attacking:%d NonAttack:%d | Spacing:%d Peak:%d | VacA:%.1f Promote:%s"),
		OccupiedAttackSlots,
		ActiveAttackSlotCount,
		GetOccupiedAttackSlotCost(),
		ActiveAttackSlotCount,
		BlockedAttackSlots,
		SideHeldAttackSlots,
		OccupiedWaitSlots,
		WaitReservations.Num(),
		OccupiedHoldingSlots,
		HoldingReservations.Num(),
		PendingRequesterCount,
		IsInitialFormationActive() ? TEXT("Initial") : TEXT("Runtime"),
		FormationRevision,
		CurrentAttackingEnemyCount,
		CurrentNonAttackSlotAttackerCount,
		CurrentSpacingViolationCount,
		PeakSpacingViolationCount,
		MaximumAttackVacancy,
		HasActiveCombatCoreEscape() ? TEXT("Ready/CoreLocal") : TEXT("Ready"));
	DrawDebugString(
		World,
		Owner->GetActorLocation() + FVector(0.0f, 0.0f, 170.0f),
		DebugText,
		nullptr,
		CurrentNonAttackSlotAttackerCount > 0 ? FColor::Red : FColor::White,
		0.12f,
		false,
		1.1f);
}

void UCombatEngagementSlotComponent::DrawCombatSpaceAnalysisDebug() const
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner || !bEnableCombatSpaceAnalysis)
	{
		return;
	}

	if (!bHasValidSpaceAnalysis || SpaceProbeEndpoints.Num() < 4)
	{
		DrawDebugString(
			World,
			Owner->GetActorLocation() + FVector(0.0f, 0.0f, 230.0f),
			TEXT("Space: Invalid NavMesh sample"),
			nullptr,
			FColor::Red,
			0.12f,
			false,
			1.0f);
		return;
	}

	const FVector DrawOrigin = SpaceProbeOrigin + FVector(0.0f, 0.0f, 8.0f);
	for (int32 ProbeIndex = 0; ProbeIndex < SpaceProbeEndpoints.Num(); ++ProbeIndex)
	{
		const bool bBlocked = SpaceProbeBlocked.IsValidIndex(ProbeIndex)
			&& SpaceProbeBlocked[ProbeIndex] != 0;
		DrawDebugLine(
			World,
			DrawOrigin,
			SpaceProbeEndpoints[ProbeIndex] + FVector(0.0f, 0.0f, 8.0f),
			bBlocked ? FColor(180, 70, 30) : FColor(70, 110, 70),
			false,
			0.12f,
			0,
			0.75f);
	}

	const int32 HalfProbeCount = SpaceProbeEndpoints.Num() / 2;
	const int32 AxisOppositeIndex = CorridorAxisProbeIndex + HalfProbeCount;
	const int32 WidthOppositeIndex = CorridorWidthProbeIndex + HalfProbeCount;
	if (SpaceProbeEndpoints.IsValidIndex(CorridorAxisProbeIndex)
		&& SpaceProbeEndpoints.IsValidIndex(AxisOppositeIndex))
	{
		DrawDebugLine(
			World,
			SpaceProbeEndpoints[CorridorAxisProbeIndex] + FVector(0.0f, 0.0f, 12.0f),
			SpaceProbeEndpoints[AxisOppositeIndex] + FVector(0.0f, 0.0f, 12.0f),
			FColor::Cyan,
			false,
			0.12f,
			0,
			4.0f);
	}
	if (SpaceProbeEndpoints.IsValidIndex(CorridorWidthProbeIndex)
		&& SpaceProbeEndpoints.IsValidIndex(WidthOppositeIndex))
	{
		DrawDebugLine(
			World,
			SpaceProbeEndpoints[CorridorWidthProbeIndex] + FVector(0.0f, 0.0f, 14.0f),
			SpaceProbeEndpoints[WidthOppositeIndex] + FVector(0.0f, 0.0f, 14.0f),
			FColor::Yellow,
			false,
			0.12f,
			0,
			4.0f);
	}
	const auto ResolveMouthColor = [](bool bRawDetected, bool bHeldDetected)
	{
		return bRawDetected
			? FColor::Red
			: (bHeldDetected ? FColor::Orange : FColor(80, 200, 255));
	};
	const FColor ForwardMouthColor = ResolveMouthColor(
		bRawCorridorForwardMouthDetected,
		bCorridorForwardMouthDetected);
	const FColor RearMouthColor = ResolveMouthColor(
		bRawCorridorRearMouthDetected,
		bCorridorRearMouthDetected);
	if (bCorridorForwardCrossSectionValid)
	{
		DrawDebugLine(
			World,
			CorridorForwardCrossSectionSide0 + FVector(0.0f, 0.0f, 18.0f),
			CorridorForwardCrossSectionSide1 + FVector(0.0f, 0.0f, 18.0f),
			ForwardMouthColor,
			false,
			0.12f,
			0,
			5.0f);
		DrawDebugSphere(
			World,
			CorridorForwardCrossSectionCenter + FVector(0.0f, 0.0f, 18.0f),
			8.0f,
			8,
			ForwardMouthColor,
			false,
			0.12f,
			0,
			2.0f);
	}
	if (bCorridorRearCrossSectionValid)
	{
		DrawDebugLine(
			World,
			CorridorRearCrossSectionSide0 + FVector(0.0f, 0.0f, 18.0f),
			CorridorRearCrossSectionSide1 + FVector(0.0f, 0.0f, 18.0f),
			RearMouthColor,
			false,
			0.12f,
			0,
			5.0f);
		DrawDebugSphere(
			World,
			CorridorRearCrossSectionCenter + FVector(0.0f, 0.0f, 18.0f),
			8.0f,
			8,
			RearMouthColor,
			false,
			0.12f,
			0,
			2.0f);
	}
	if (IsCorridorFormationActive())
	{
		const FColor Side0Color = IsMouthMixedFanSide(0) ? FColor::Orange : FColor::Purple;
		const FColor Side1Color = IsMouthMixedFanSide(1) ? FColor::Orange : FColor::Purple;
		DrawDebugDirectionalArrow(
			World,
			DrawOrigin,
			DrawOrigin + CorridorFormationRearDirection * 180.0f,
			30.0f,
			Side0Color,
			false,
			0.12f,
			0,
			4.0f);
		DrawDebugDirectionalArrow(
			World,
			DrawOrigin,
			DrawOrigin - CorridorFormationRearDirection * 180.0f,
			30.0f,
			Side1Color,
			false,
			0.12f,
			0,
			4.0f);
	}
	else if (IsPocketFormationActive())
	{
		DrawDebugDirectionalArrow(
			World,
			DrawOrigin,
			DrawOrigin + PocketFormationDirection * 180.0f,
			30.0f,
			FColor::Orange,
			false,
			0.12f,
			0,
			4.0f);
	}

	const auto ResolveModeText = [](EBHCombatSpaceMode Mode)
	{
		switch (Mode)
		{
		case EBHCombatSpaceMode::Corridor:
			return TEXT("Corridor");
		case EBHCombatSpaceMode::Pocket:
			return TEXT("Pocket");
		default:
			return TEXT("Open");
		}
	};
	const TCHAR* CurrentModeText = ResolveModeText(CurrentSpaceMode);
	const TCHAR* CandidateModeText = ResolveModeText(CandidateSpaceMode);
	int32 MaximumDynamicRowLaneCount = 0;
	int32 WaitRowSideCounts[2] = { 0, 0 };
	int32 HoldingRowSideCounts[2] = { 0, 0 };
	for (const FCorridorRowSlot& RowSlot : CorridorWaitRowLayout)
	{
		MaximumDynamicRowLaneCount = FMath::Max(
			MaximumDynamicRowLaneCount,
			RowSlot.LaneCount);
		if (FMath::IsWithin(RowSlot.SideIndex, 0, 2))
		{
			++WaitRowSideCounts[RowSlot.SideIndex];
		}
	}
	for (const FCorridorRowSlot& RowSlot : CorridorHoldingRowLayout)
	{
		MaximumDynamicRowLaneCount = FMath::Max(
			MaximumDynamicRowLaneCount,
			RowSlot.LaneCount);
		if (FMath::IsWithin(RowSlot.SideIndex, 0, 2))
		{
			++HoldingRowSideCounts[RowSlot.SideIndex];
		}
	}
	int32 ValidAttackCandidateCount = 0;
	int32 SelectedAttackCandidateCount = 0;
	for (int32 ProbeIndex = 0; ProbeIndex < CorridorAttackProbeLocations.Num(); ++ProbeIndex)
	{
		ValidAttackCandidateCount += CorridorAttackProbeValid.IsValidIndex(ProbeIndex)
			&& CorridorAttackProbeValid[ProbeIndex] != 0 ? 1 : 0;
		SelectedAttackCandidateCount += CorridorAttackProbeSelected.IsValidIndex(ProbeIndex)
			&& CorridorAttackProbeSelected[ProbeIndex] != 0 ? 1 : 0;
	}
	const auto ResolveMouthStateText = [](bool bRawDetected, bool bHeldDetected)
	{
		return bRawDetected
			? TEXT("Raw")
			: (bHeldDetected ? TEXT("Held") : TEXT("No"));
	};
	const TCHAR* ForwardMouthStateText = ResolveMouthStateText(
		bRawCorridorForwardMouthDetected,
		bCorridorForwardMouthDetected);
	const TCHAR* RearMouthStateText = ResolveMouthStateText(
		bRawCorridorRearMouthDetected,
		bCorridorRearMouthDetected);
	const auto ResolveMouthKindText = [](EMouthMixedKind Kind)
	{
		switch (Kind)
		{
		case EMouthMixedKind::Side0Open:
			return TEXT("Side0Open");
		case EMouthMixedKind::Side1Open:
			return TEXT("Side1Open");
		case EMouthMixedKind::DoubleMouth:
			return TEXT("Double");
		default:
			return TEXT("No");
		}
	};
	const TCHAR* MouthMixedKindText = ResolveMouthKindText(
		bMouthMixedActive ? ActiveMouthMixedKind : EMouthMixedKind::None);
	const TCHAR* PendingMouthMixedKindText = ResolveMouthKindText(PendingMouthMixedKind);
	const FString SpaceDebugText = FString::Printf(
		TEXT("Space %s | Candidate:%s %.1fs | Width:%.0f Mouth F:%s %.0f R:%s %.0f Mixed:%s Next:%s %.1fs | Edge:%.0f/+%.0f Axis:%.0f Ratio:%.2f | PocketArc:%.0f Wall:%.2f | Sides:%d AttackLanes:%d RowLanes:%d RowSides W:%d/%d H:%d/%d AttackCand:%d/%d Selected:%d ActiveA:%d (%d/%d)"),
		CurrentModeText,
		CandidateModeText,
		SpaceModeTransitionElapsed,
		EstimatedCorridorWidth,
		ForwardMouthStateText,
		EstimatedCorridorForwardWidth,
		RearMouthStateText,
		EstimatedCorridorRearWidth,
		MouthMixedKindText,
		PendingMouthMixedKindText,
		MouthMixedEvidenceElapsed,
		EstimatedCorridorEdgeNearDistance,
		EstimatedCorridorEdgeClearanceDifference,
		EstimatedCorridorAxisLength,
		EstimatedCorridorAspectRatio,
		EstimatedPocketOpenArc,
		EstimatedPocketBlockedFraction,
		IsCorridorFormationActive() ? 2 : 0,
		IsCorridorFormationActive() ? ActiveCorridorLaneCount : 0,
		IsCorridorFormationActive() ? MaximumDynamicRowLaneCount : 0,
		IsCorridorFormationActive() ? WaitRowSideCounts[0] : 0,
		IsCorridorFormationActive() ? WaitRowSideCounts[1] : 0,
		IsCorridorFormationActive() ? HoldingRowSideCounts[0] : 0,
		IsCorridorFormationActive() ? HoldingRowSideCounts[1] : 0,
		IsCorridorFormationActive() ? ValidAttackCandidateCount : 0,
		IsCorridorFormationActive() ? CorridorAttackProbeLocations.Num() : 0,
		IsCorridorFormationActive() ? SelectedAttackCandidateCount : 0,
		GetActiveAttackSlotCount(),
		IsCorridorFormationActive() ? ActiveCorridorSide0AttackSlotCount : 0,
		IsCorridorFormationActive() ? ActiveCorridorSide1AttackSlotCount : 0);
	DrawDebugString(
		World,
		Owner->GetActorLocation() + FVector(0.0f, 0.0f, 230.0f),
		SpaceDebugText,
		nullptr,
		CurrentSpaceMode == EBHCombatSpaceMode::Corridor
			? FColor::Cyan
			: (CurrentSpaceMode == EBHCombatSpaceMode::Pocket ? FColor::Orange : FColor::White),
		0.12f,
		false,
		1.0f);

	if (!IsCorridorFormationActive())
	{
		return;
	}

	int32 ReservedCounts[4][2] = {};
	int32 ArrivedCounts[4][2] = {};
	const float ArrivedDistanceSquared = FMath::Square(FMath::Max(1.0f, PromotionArrivalRadius));
	auto AccumulateReservedLayer = [this, ArrivedDistanceSquared, &ReservedCounts, &ArrivedCounts](
		const TArray<TWeakObjectPtr<AActor>>& Reservations,
		const TArray<FCorridorRowSlot>& Layout,
		int32 LayerIndex)
	{
		for (int32 SlotIndex = 0; SlotIndex < Reservations.Num(); ++SlotIndex)
		{
			AActor* Requester = Reservations[SlotIndex].Get();
			if (!Requester || !Layout.IsValidIndex(SlotIndex))
			{
				continue;
			}
			const int32 SideIndex = Layout[SlotIndex].SideIndex;
			if (!FMath::IsWithin(SideIndex, 0, 2))
			{
				continue;
			}
			++ReservedCounts[LayerIndex][SideIndex];
			if (FVector::DistSquared2D(
				Requester->GetActorLocation(),
				Layout[SlotIndex].WorldLocation) <= ArrivedDistanceSquared)
			{
				++ArrivedCounts[LayerIndex][SideIndex];
			}
		}
	};
	for (int32 SlotIndex = 0; SlotIndex < AttackReservations.Num(); ++SlotIndex)
	{
		AActor* Requester = AttackReservations[SlotIndex].Get();
		if (!Requester || !ActiveCorridorAttackLayout.IsValidIndex(SlotIndex))
		{
			continue;
		}
		const int32 SideIndex = ActiveCorridorAttackLayout[SlotIndex].SideIndex;
		if (!FMath::IsWithin(SideIndex, 0, 2))
		{
			continue;
		}
		++ReservedCounts[0][SideIndex];
		if (FVector::DistSquared2D(
			Requester->GetActorLocation(),
			ActiveCorridorAttackLayout[SlotIndex].WorldLocation) <= ArrivedDistanceSquared)
		{
			++ArrivedCounts[0][SideIndex];
		}
	}
	AccumulateReservedLayer(WaitReservations, CorridorWaitRowLayout, 1);
	AccumulateReservedLayer(HoldingReservations, CorridorHoldingRowLayout, 2);

	const FVector AxisDirection = CorridorFormationRearDirection.GetSafeNormal2D();
	for (const FEngagementQueueEntry& Entry : EngagementQueue)
	{
		AActor* Requester = Entry.Requester.Get();
		int32 ReservedSlotIndex = INDEX_NONE;
		if (!Requester
			|| FindReservation(AttackReservations, Requester, ReservedSlotIndex)
			|| FindReservation(WaitReservations, Requester, ReservedSlotIndex)
			|| FindReservation(HoldingReservations, Requester, ReservedSlotIndex))
		{
			continue;
		}

		int32 PendingIndex = INDEX_NONE;
		FVector PendingLocation;
		if (!GetCorridorPendingWorldLocationForRequester(
			Requester,
			PendingIndex,
			PendingLocation))
		{
			continue;
		}
		const int32 SideIndex = FVector::DotProduct(
			PendingLocation - EngagementAnchorLocation,
			AxisDirection) >= 0.0f ? 0 : 1;
		++ReservedCounts[3][SideIndex];
		if (FVector::DistSquared2D(
			Requester->GetActorLocation(),
			PendingLocation) <= ArrivedDistanceSquared)
		{
			++ArrivedCounts[3][SideIndex];
		}
	}

	const FString OccupancyDebugText = FString::Printf(
		TEXT("MouthSides Layout W:%d/%d H:%d/%d | Assigned A:%d/%d W:%d/%d H:%d/%d TargetQ:%d/%d | NearTarget A:%d/%d W:%d/%d H:%d/%d Q:%d/%d"),
		WaitRowSideCounts[0],
		WaitRowSideCounts[1],
		HoldingRowSideCounts[0],
		HoldingRowSideCounts[1],
		ReservedCounts[0][0], ReservedCounts[0][1],
		ReservedCounts[1][0], ReservedCounts[1][1],
		ReservedCounts[2][0], ReservedCounts[2][1],
		ReservedCounts[3][0], ReservedCounts[3][1],
		ArrivedCounts[0][0], ArrivedCounts[0][1],
		ArrivedCounts[1][0], ArrivedCounts[1][1],
		ArrivedCounts[2][0], ArrivedCounts[2][1],
		ArrivedCounts[3][0], ArrivedCounts[3][1]);
	DrawDebugString(
		World,
		Owner->GetActorLocation() + FVector(0.0f, 0.0f, 210.0f),
		OccupancyDebugText,
		nullptr,
		bMouthMixedActive ? FColor::Orange : FColor::Silver,
		0.12f,
		false,
		0.9f);
}
