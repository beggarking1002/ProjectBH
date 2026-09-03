// Copyright ProjectBH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UNavigationPath;
class UWorld;

/**
 * Shared, stateless navigation and path geometry queries.
 *
 * Keeps the definition of a usable AI path consistent across controllers and
 * combat formation policies: valid, complete, and containing at least two points.
 */
class PROJECTBH_API FBHNavigationQuery final
{
public:
	static UNavigationPath* FindCompletePath(
		UWorld* World,
		const FVector& Start,
		const FVector& Destination,
		AActor* QueryOwner,
		bool bRequireMovementSegment = false);

	static float PointSegmentDistanceSquared2D(
		const FVector& Point,
		const FVector& SegmentStart,
		const FVector& SegmentEnd);

	/** Requires at least two points; intended for paths returned with bRequireMovementSegment. */
	static float PointPolylineDistanceSquared2D(
		const FVector& Point,
		TConstArrayView<FVector> PathPoints);
};
