// Copyright ProjectBH. All Rights Reserved.

#include "BHNavigationQuery.h"

#include "GameFramework/Actor.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

UNavigationPath* FBHNavigationQuery::FindCompletePath(
	UWorld* World,
	const FVector& Start,
	const FVector& Destination,
	AActor* QueryOwner,
	bool bRequireMovementSegment)
{
	if (!World || !QueryOwner)
	{
		return nullptr;
	}

	UNavigationPath* NavigationPath = UNavigationSystemV1::FindPathToLocationSynchronously(
		World,
		Start,
		Destination,
		QueryOwner);
	return NavigationPath
		&& NavigationPath->IsValid()
		&& !NavigationPath->IsPartial()
		&& (!bRequireMovementSegment || NavigationPath->PathPoints.Num() >= 2)
		? NavigationPath
		: nullptr;
}

float FBHNavigationQuery::PointSegmentDistanceSquared2D(
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
}

float FBHNavigationQuery::PointPolylineDistanceSquared2D(
	const FVector& Point,
	TConstArrayView<FVector> PathPoints)
{
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (int32 PointIndex = 1; PointIndex < PathPoints.Num(); ++PointIndex)
	{
		BestDistanceSquared = FMath::Min(
			BestDistanceSquared,
			PointSegmentDistanceSquared2D(
				Point,
				PathPoints[PointIndex - 1],
				PathPoints[PointIndex]));
	}
	return BestDistanceSquared;
}
