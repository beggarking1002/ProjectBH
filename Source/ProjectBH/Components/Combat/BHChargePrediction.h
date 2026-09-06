// Copyright ProjectBH. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace BHChargePrediction
{
// Constant-velocity interception estimates lead, not actual displacement:
// the montage still owns travel. Bound it for dodges and finite root motion.
// Caller first validates that the current target is within MaximumReach.
// No positive intercept uses distance/speed as a heuristic, not a promised hit.
inline FVector PredictOffset(
	const FVector& ToTarget, const FVector& TargetVelocity, float TravelSpeed,
	float MaximumTime, float MaximumLead, float MaximumReach)
{
	const FVector Relative(ToTarget.X, ToTarget.Y, 0.0f);
	const FVector Velocity(TargetVelocity.X, TargetVelocity.Y, 0.0f);
	if (TravelSpeed <= UE_KINDA_SMALL_NUMBER || MaximumTime <= 0.0f || MaximumLead <= 0.0f
		|| !FMath::IsFinite(TravelSpeed) || !FMath::IsFinite(MaximumTime)
		|| !FMath::IsFinite(MaximumLead) || !FMath::IsFinite(MaximumReach)
		|| Relative.ContainsNaN() || Velocity.ContainsNaN())
	{
		return FVector::ZeroVector;
	}

	const double A = Velocity.SizeSquared() - FMath::Square(static_cast<double>(TravelSpeed));
	const double B = 2.0 * FVector::DotProduct(Relative, Velocity);
	const double C = Relative.SizeSquared();
	double Time = Relative.Size() / TravelSpeed;
	if (FMath::Abs(A) < UE_KINDA_SMALL_NUMBER)
	{
		if (B < -UE_KINDA_SMALL_NUMBER) { Time = -C / B; }
	}
	else
	{
		const double Discriminant = B * B - 4.0 * A * C;
		if (Discriminant >= 0.0)
		{
			const double Root = FMath::Sqrt(Discriminant);
			const double T1 = (-B - Root) / (2.0 * A);
			const double T2 = (-B + Root) / (2.0 * A);
			if (T1 > 0.0 && T2 > 0.0) { Time = FMath::Min(T1, T2); }
			else if (T1 > 0.0) { Time = T1; }
			else if (T2 > 0.0) { Time = T2; }
		}
	}

	FVector Lead = (Velocity * FMath::Clamp(Time, 0.0, static_cast<double>(MaximumTime)))
		.GetClampedToMaxSize(MaximumLead);
	const double Reach = FMath::Max(0.0f, MaximumReach);
	if ((Relative + Lead).SizeSquared() > Reach * Reach && !Lead.IsNearlyZero())
	{
		// Shorten only prediction, never extend the authored charge to chase it.
		const double Dot = FVector::DotProduct(Relative, Lead);
		const double LengthSquared = Lead.SizeSquared();
		const double Discriminant = Dot * Dot - LengthSquared * (C - Reach * Reach);
		const double Fraction = (-Dot + FMath::Sqrt(FMath::Max(0.0, Discriminant))) / LengthSquared;
		Lead *= FMath::Clamp(Fraction, 0.0, 1.0);
	}
	return Lead;
}
}
