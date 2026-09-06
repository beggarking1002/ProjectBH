// Copyright ProjectBH. All Rights Reserved.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "../Components/Combat/BHChargePrediction.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHChargePredictionTest,
	"ProjectBH.Combat.ChargePrediction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBHChargePredictionTest::RunTest(const FString& Parameters)
{
	using BHChargePrediction::PredictOffset;
	const FVector Target(600.0, 0.0, 0.0);
	TestTrue(TEXT("Stationary target has no lead"),
		PredictOffset(Target, FVector::ZeroVector, 1000, 1, 500, 1500).IsNearlyZero());
	TestTrue(TEXT("Prediction can be disabled"),
		PredictOffset(Target, FVector(0, 300, 0), 1000, 0, 500, 1500).IsNearlyZero());
	// Independent intercept oracle: sqrt(600^2 + (300*t)^2) = 1000*t.
	const FVector SideLead = PredictOffset(Target, FVector(0, 300, 200), 1000, 1, 500, 1500);
	TestTrue(TEXT("Lateral interception solves travel time"),
		FMath::Abs(SideLead.Y - 300.0 * 600.0 / FMath::Sqrt(1000000.0 - 90000.0)) < 0.01);
	TestEqual(TEXT("Vertical target velocity does not steer charge"), SideLead.Z, 0.0);
	const FVector Mirrored = PredictOffset(Target, FVector(0, -300, 0), 1000, 1, 500, 1500);
	TestTrue(TEXT("Left/right symmetry"), FMath::Abs(Mirrored.Y + SideLead.Y) < 0.01);
	const FVector FastLead = PredictOffset(Target, FVector(2000, 0, 0), 1000, 0.75f, 300, 800);
	TestTrue(TEXT("Uncatchable target produces finite bounded aim"), !FastLead.ContainsNaN());
	TestTrue(TEXT("Prediction does not exceed authored reach"),
		FMath::Abs((Target + FastLead).Size() - 800.0) < 0.01);
	TestTrue(TEXT("Lead distance cap"),
		PredictOffset(Target, FVector(0, 2000, 0), 1000, 1, 100, 1500).Size() <= 100.01);
	TestTrue(TEXT("Lead time cap"),
		PredictOffset(Target, FVector(0, 300, 0), 1000, 0.1f, 500, 1500).Equals(FVector(0, 30, 0), 0.01));
	TestTrue(TEXT("Zero travel speed is safe"),
		PredictOffset(Target, FVector(100, 0, 0), 0, 1, 500, 1500).IsNearlyZero());
	return true;
}
#endif
