// Copyright ProjectBH. All Rights Reserved.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "../Diagnostics/BHCombatDiagnosticsSubsystem.h"
#include "../Combat/Engagement/BHLargeEnemyEngagementPolicy.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHDiagnosticsSessionTest, "ProjectBH.Diagnostics.EmptySessionCommands", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBHDiagnosticsSessionTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	auto* D = World->GetSubsystem<UBHCombatDiagnosticsSubsystem>();
	if (!TestNotNull(TEXT("Authority game world creates diagnostics"), D)) { World->DestroyWorld(false); return false; }
	TestNull(TEXT("Default disabled accessor"), UBHCombatDiagnosticsSubsystem::Get(World));
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics/ProjectBH"));
	TArray<FString> Before, After;
	IFileManager::Get().FindFiles(Before, *(Directory / TEXT("*")), true, false);
	D->Tick(1.0f);
	IFileManager::Get().FindFiles(After, *(Directory / TEXT("*")), true, false);
	TestEqual(TEXT("Disabled tick creates no files"), After.Num(), Before.Num());
	const auto Command = [&](const TCHAR* Text) { return IConsoleManager::Get().ProcessUserConsoleInput(Text, *GLog, World); };
	TestTrue(TEXT("Start command"), Command(TEXT("bh.Diagnostics.Start Automation_Empty")));
	TestNotNull(TEXT("Start activates this world"), UBHCombatDiagnosticsSubsystem::Get(World));
	TestFalse(TEXT("Trace default off"), D->IsTracing());
	TestTrue(TEXT("Status command"), Command(TEXT("bh.Diagnostics.Status")));
	TestTrue(TEXT("Stop command"), Command(TEXT("bh.Diagnostics.Stop")));
	TestNull(TEXT("Stop deactivates recording"), UBHCombatDiagnosticsSubsystem::Get(World));
	IFileManager::Get().FindFiles(After, *(Directory / TEXT("*")), true, false);
	TArray<FString> Created;
	for (const FString& File : After) if (!Before.Contains(File)) Created.Add(File);
	TestEqual(TEXT("Stop writes four actual empty-session artifacts"), Created.Num(), 4);
	for (const FString& File : Created)
	{
		FString Contents; TestTrue(TEXT("Readable output"), FFileHelper::LoadFileToString(Contents, *(Directory / File)));
		if (File.EndsWith(TEXT("Summary.json")))
		{
			TSharedPtr<FJsonObject> Json;
			if (TestTrue(TEXT("Valid summary JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Json)))
			{
				TestEqual(TEXT("No fabricated handovers"), Json->GetNumberField(TEXT("HandoverAttemptCount")), 0.0);
				TestEqual(TEXT("No fabricated escape success"), Json->GetNumberField(TEXT("CoreEscapeSuccessCount")), 0.0);
				TestEqual(TEXT("Correct scenario"), Json->GetStringField(TEXT("ScenarioName")), FString(TEXT("Automation_Empty")));
			}
		}
		else
		{
			TArray<FString> Lines; Contents.ParseIntoArrayLines(Lines, true);
			TestEqual(TEXT("Empty session CSV has header only"), Lines.Num(), 1);
			TArray<uint8> Bytes; FFileHelper::LoadFileToArray(Bytes, *(Directory / File));
			TestTrue(TEXT("Excel UTF-8 BOM"), Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF);
		}
	}
	Command(TEXT("bh.Diagnostics.Start Automation_Reset")); Command(TEXT("bh.Diagnostics.TraceAlgorithms 1"));
	TestTrue(TEXT("Trace toggle on"), D->IsTracing());
	Command(TEXT("bh.Diagnostics.TraceAlgorithms 0")); TestFalse(TEXT("Trace toggle off"), D->IsTracing());
	Command(TEXT("bh.Diagnostics.Reset")); TestNull(TEXT("Reset deactivates"), UBHCombatDiagnosticsSubsystem::Get(World));
	IFileManager::Get().FindFiles(Before, *(Directory / TEXT("*")), true, false); TestEqual(TEXT("Reset produces no output"), Before.Num(), After.Num());
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHDiagnosticsPolicyTest, "ProjectBH.Diagnostics.LargePolicyObservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBHDiagnosticsPolicyTest::RunTest(const FString& Parameters)
{
	FBHLargeEnemyAttackPolicyInput Input; Input.ActiveAttackCapacity = 2; Input.RequesterCost = 1;
	FBHAttackSlotPolicyCandidate A; A.SlotIndex = 0; A.PathScore = 10; A.bPreferredCorridorSide = false;
	FBHAttackSlotPolicyCandidate B; B.SlotIndex = 1; B.PathScore = 100; B.bPreferredCorridorSide = true;
	Input.Candidates = { A, B };
	TArray<FBHLargeReservationEvaluation> Rows;
	const auto Plain = FBHLargeEnemyEngagementPolicy::BuildAttackReservationPlan(Input);
	const auto Observed = FBHLargeEnemyEngagementPolicy::BuildAttackReservationPlan(Input, &Rows);
	TestEqual(TEXT("Preferred side outranks path cost (independent fixture)"), Plain.AttackSlotIndex, 1);
	TestEqual(TEXT("Observation preserves selected slot"), Observed.AttackSlotIndex, Plain.AttackSlotIndex);
	TestEqual(TEXT("Observation preserves victims"), Observed.NormalOwnersToYield.Num(), Plain.NormalOwnersToYield.Num());
	TestEqual(TEXT("One row per evaluated candidate"), Rows.Num(), 2);
	Input.RequesterCost = 3; Input.bAllowRequesterOverCapacityWhenAlone = false; Rows.Reset();
	const auto Rejected = FBHLargeEnemyEngagementPolicy::BuildAttackReservationPlan(Input, &Rows);
	TestFalse(TEXT("Capacity rejects all candidates"), Rejected.IsValid());
	for (const auto& Row : Rows) { TestFalse(TEXT("Rejected row valid flag"), Row.bValid); TestEqual(TEXT("Actual rejection reason"), FString(Row.RejectedReason), FString(TEXT("CapacityExceeded"))); }
	Input.bAllowRequesterOverCapacityWhenAlone = true;
	TestTrue(TEXT("Existing oversized-alone exception retained"), FBHLargeEnemyEngagementPolicy::BuildAttackReservationPlan(Input).IsValid());
	return true;
}
#endif
