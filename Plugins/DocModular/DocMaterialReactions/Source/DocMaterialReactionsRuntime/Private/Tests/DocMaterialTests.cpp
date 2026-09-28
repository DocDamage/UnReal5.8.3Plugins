#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocMaterialReactionTypes.h"
#include "DocMaterialProfile.h"
#include "DocMaterialReactionDefinition.h"
#include "DocReactiveMaterialComponent.h"
#include "DocMaterialReactionSubsystem.h"
#include "GameplayTagsManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

namespace DocMaterialTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FGameplayTag GetOrCreateTestTag(const FName& TagName = TEXT("Doc.Test.Heat"))
	{
		UGameplayTagsManager& Mgr = UGameplayTagsManager::Get();
		FGameplayTag Tag = Mgr.RequestGameplayTag(TagName, false);
		if (!Tag.IsValid())
		{
			Tag = Mgr.AddNativeGameplayTag(TagName);
		}
		return Tag;
	}

	UDocMaterialReactionDefinition* CreateTestReaction(UObject* Outer, FName ReactionId, FName GroupId, int32 Priority,
		FGameplayTag Channel, float ActThresh, float DeactThresh, float Dwell, float FuelRate, float MoistureRate,
		float PhaseRate, float CharRate, bool bReqFuel, bool bReqMoisture, bool bPropagate = false, float Radius = 200.0f,
		bool bSelfSustaining = false)
	{
		UDocMaterialReactionDefinition* Def = NewObject<UDocMaterialReactionDefinition>(Outer);
		Def->ReactionId = ReactionId;
		Def->ExclusiveGroup = GroupId;
		Def->Priority = Priority;
		Def->ExposureChannel = Channel;
		Def->ActivationThreshold = ActThresh;
		Def->DeactivationThreshold = DeactThresh;
		Def->DwellDuration = Dwell;
		Def->FuelConsumptionRate = FuelRate;
		Def->MoistureConsumptionRate = MoistureRate;
		Def->PhaseChangeRate = PhaseRate;
		Def->CharRate = CharRate;
		Def->bRequiresFuel = bReqFuel;
		Def->bRequiresMoisture = bReqMoisture;
		Def->bCanPropagate = bPropagate;
		Def->PropagationRadius = Radius;
		Def->PropagationExposureChannel = Channel;
		Def->PropagationIntensity = 1.0f;
		Def->bSelfSustaining = bSelfSustaining;
		return Def;
	}

	UDocMaterialReactionDefinition* CreateWetting(UObject* Outer, FGameplayTag WetTag, float GainRate)
	{
		UDocMaterialReactionDefinition* Def = NewObject<UDocMaterialReactionDefinition>(Outer);
		Def->ReactionId = TEXT("Wet");
		Def->ExposureChannel = WetTag;
		Def->ActivationThreshold = 0.1f;
		Def->DeactivationThreshold = 0.05f;
		Def->MoistureGainRate = GainRate;
		return Def;
	}

	UDocMaterialProfile* CreateTestProfile(UObject* Outer, FName MaterialId, float Fuel, float Moisture, float Phase,
		const TArray<UDocMaterialReactionDefinition*>& Reactions)
	{
		UDocMaterialProfile* Profile = NewObject<UDocMaterialProfile>(Outer);
		Profile->MaterialId = MaterialId;
		Profile->InitialFuel = Fuel;
		Profile->MaxFuel = Fuel;
		Profile->InitialMoisture = Moisture;
		Profile->InitialPhaseFraction = Phase;
		for (UDocMaterialReactionDefinition* Def : Reactions)
		{
			Profile->AllowedReactions.Add(Def);
		}
		return Profile;
	}

	UDocReactiveMaterialComponent* SpawnReactive(UWorld* World, const FVector& Location = FVector::ZeroVector, FName ObjectId = NAME_None)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		// A plain AActor has no root; without one SetActorLocation is ignored and every actor sits at the origin.
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		UDocReactiveMaterialComponent* Comp = NewObject<UDocReactiveMaterialComponent>(Actor);
		Comp->ReactiveObjectId = ObjectId;
		Actor->AddInstanceComponent(Comp);
		Comp->RegisterComponent();
		return Comp;
	}

	FDocExposureSample MakeSample(FName SourceId, FGameplayTag Channel, float Intensity, int32 Sequence = 0)
	{
		FDocExposureSample Sample;
		Sample.SourceId = SourceId;
		Sample.Channel = Channel;
		Sample.Intensity = Intensity;
		Sample.Sequence = Sequence;
		return Sample;
	}
}

// MAT-01: Threshold, dwell, and hysteresis prevent premature/chattering ignition
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMaterialIgnitionThresholdTest, FAutomationTestBase, "Doc.Material.IgnitionThreshold", DocMaterialTests::Flags)
bool FDocMaterialIgnitionThresholdTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	UDocReactiveMaterialComponent* Comp = DocMaterialTests::SpawnReactive(World);
	const FGameplayTag HeatTag = DocMaterialTests::GetOrCreateTestTag();

	UDocMaterialReactionDefinition* BurnDef = DocMaterialTests::CreateTestReaction(World, TEXT("Burn"), TEXT("Thermal"), 1, HeatTag,
		1.0f, 0.4f, 1.0f, 10.0f, 0.0f, 0.0f, 0.1f, true, false);
	UDocMaterialProfile* Profile = DocMaterialTests::CreateTestProfile(World, TEXT("Wood"), 100.0f, 0.0f, 0.0f, { BurnDef });
	TestTrue(TEXT("Profile initializes"), Comp->InitializeFromProfile(Profile));

	int32 Starts = 0;
	int32 Ends = 0;
	Comp->OnReactionStartedNative.AddLambda([&Starts](FName, const FDocReactionTransition&) { ++Starts; });
	Comp->OnReactionEndedNative.AddLambda([&Ends](FName, const FDocReactionTransition&) { ++Ends; });

	TestTrue(TEXT("Heat source accepted"), Comp->AddExposureSource(DocMaterialTests::MakeSample(TEXT("Torch"), HeatTag, 0.8f)));

	// Below threshold: nothing happens.
	Comp->StepSimulation(0.5f, 0.5);
	TestFalse(TEXT("Below threshold does not ignite"), Comp->IsReactionActive(TEXT("Burn")));

	// Chatter around the threshold resets the dwell each time exposure dips.
	Comp->UpdateExposureSource(TEXT("Torch"), 1.2f);
	Comp->StepSimulation(0.6f, 1.1);
	Comp->UpdateExposureSource(TEXT("Torch"), 0.9f);
	Comp->StepSimulation(0.1f, 1.2);
	Comp->UpdateExposureSource(TEXT("Torch"), 1.2f);
	Comp->StepSimulation(0.6f, 1.8);
	TestFalse(TEXT("Dwell restarts after a dip, so 0.6 s + 0.6 s split by a dip does not ignite"), Comp->IsReactionActive(TEXT("Burn")));
	FDocReactionInstance Pending;
	TestTrue(TEXT("Pending instance exists"), Comp->GetReactionInstance(TEXT("Burn"), Pending));
	TestEqual(TEXT("Pending state"), Pending.State, EDocReactionState::PendingDwell);

	// Completing a continuous dwell ignites once.
	Comp->StepSimulation(0.5f, 2.3);
	TestTrue(TEXT("Continuous dwell met: reaction active"), Comp->IsReactionActive(TEXT("Burn")));
	TestEqual(TEXT("Exactly one start event"), Starts, 1);

	// Between the thresholds: hysteresis holds.
	Comp->UpdateExposureSource(TEXT("Torch"), 0.6f);
	Comp->StepSimulation(0.5f, 2.8);
	TestTrue(TEXT("Hysteresis prevents premature extinguishing"), Comp->IsReactionActive(TEXT("Burn")));
	TestEqual(TEXT("No end event inside the hysteresis band"), Ends, 0);

	// Below the deactivation threshold: ends once.
	Comp->UpdateExposureSource(TEXT("Torch"), 0.2f);
	Comp->StepSimulation(0.5f, 3.3);
	TestFalse(TEXT("Dropping below deactivation threshold ends the reaction"), Comp->IsReactionActive(TEXT("Burn")));
	TestEqual(TEXT("Exactly one end event"), Ends, 1);

	// A listener cannot advance the simulation re-entrantly (no unbounded transitions in one callback).
	bool bNestedRejected = false;
	Comp->OnReactionStartedNative.AddLambda([Comp, &bNestedRejected](FName, const FDocReactionTransition&)
	{
		Comp->StepSimulation(1.0f, 100.0);
		bNestedRejected = Comp->GetLastRejectReason() == FName(TEXT("RecursionLimit"));
	});
	Comp->UpdateExposureSource(TEXT("Torch"), 2.0f);
	const float FuelBefore = Comp->CurrentState.Fuel;
	Comp->StepSimulation(1.0f, 4.3);
	TestTrue(TEXT("Nested step from a listener is refused"), bNestedRejected);
	TestEqual(TEXT("Only the outer step consumed fuel"), Comp->CurrentState.Fuel, FuelBefore - 10.0f);

	return true;
}

// MAT-02: Burning cannot consume below zero or regenerate fuel on restart
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMaterialFiniteFuelTest, FAutomationTestBase, "Doc.Material.FiniteFuel", DocMaterialTests::Flags)
bool FDocMaterialFiniteFuelTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	const FGameplayTag HeatTag = DocMaterialTests::GetOrCreateTestTag();
	UDocMaterialReactionDefinition* BurnDef = DocMaterialTests::CreateTestReaction(World, TEXT("Burn"), NAME_None, 1, HeatTag,
		1.0f, 0.5f, 0.0f, 20.0f, 0.0f, 0.0f, 0.1f, true, false, false, 200.0f, true);

	// Depletion clamps at zero and reports the consumed total.
	UDocReactiveMaterialComponent* Twig = DocMaterialTests::SpawnReactive(World);
	Twig->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Twig"), 10.0f, 0.0f, 0.0f, { BurnDef }));

	FDocReactionTransition EndTransition;
	Twig->OnReactionEndedNative.AddLambda([&EndTransition](FName, const FDocReactionTransition& T) { EndTransition = T; });

	TestTrue(TEXT("Burning started"), Twig->RequestManualReaction(TEXT("Burn")));
	Twig->StepSimulation(1.0f, 1.0);
	TestEqual(TEXT("Fuel clamped at 0"), Twig->CurrentState.Fuel, 0.0f);
	TestFalse(TEXT("Burning stops when fuel is depleted"), Twig->IsReactionActive(TEXT("Burn")));
	TestEqual(TEXT("End transition is Depleted"), EndTransition.NewState, EDocReactionState::Depleted);
	TestEqual(TEXT("End transition reports exactly the available fuel"), EndTransition.ConsumedFuel, 10.0f);

	Twig->StepSimulation(1.0f, 2.0);
	TestEqual(TEXT("Fuel cannot become negative"), Twig->CurrentState.Fuel, 0.0f);
	TestFalse(TEXT("Cannot reignite depleted fuel"), Twig->RequestManualReaction(TEXT("Burn")));
	TestEqual(TEXT("Reject reason names the resource"), Twig->GetLastRejectReason(), FName(TEXT("FuelDepleted")));
	TestEqual(TEXT("Fuel remained zero"), Twig->CurrentState.Fuel, 0.0f);

	// Extinguish and reignite continue from the remaining fuel.
	UDocMaterialReactionDefinition* SlowBurn = DocMaterialTests::CreateTestReaction(World, TEXT("Burn"), NAME_None, 1, HeatTag,
		1.0f, 0.5f, 0.0f, 5.0f, 0.0f, 0.0f, 0.0f, true, false, false, 200.0f, true);
	UDocReactiveMaterialComponent* Log = DocMaterialTests::SpawnReactive(World);
	Log->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Log"), 10.0f, 0.0f, 0.0f, { SlowBurn }));
	Log->RequestManualReaction(TEXT("Burn"));
	Log->StepSimulation(1.0f, 1.0);
	TestEqual(TEXT("Half the fuel burned"), Log->CurrentState.Fuel, 5.0f);
	Log->ExtinguishReactions(TEXT("Test"));
	TestEqual(TEXT("Extinguish does not refund fuel"), Log->CurrentState.Fuel, 5.0f);
	TestTrue(TEXT("Reignite allowed with fuel left"), Log->RequestManualReaction(TEXT("Burn")));
	TestEqual(TEXT("Reignite does not regenerate fuel"), Log->CurrentState.Fuel, 5.0f);
	Log->StepSimulation(2.0f, 3.0);
	TestEqual(TEXT("Remaining fuel burns out"), Log->CurrentState.Fuel, 0.0f);
	TestFalse(TEXT("Depleted after the remainder"), Log->IsReactionActive(TEXT("Burn")));

	return true;
}

// MAT-03: Wetting/ignition boundary order is reproducible and documented
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMaterialExtinguishOrderingTest, FAutomationTestBase, "Doc.Material.ExtinguishOrdering", DocMaterialTests::Flags)
bool FDocMaterialExtinguishOrderingTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	const FGameplayTag HeatTag = DocMaterialTests::GetOrCreateTestTag();
	const FGameplayTag WetTag = DocMaterialTests::GetOrCreateTestTag(TEXT("Doc.Test.Wet"));

	UDocMaterialReactionDefinition* Burn = DocMaterialTests::CreateTestReaction(World, TEXT("Burn"), NAME_None, 10, HeatTag,
		1.0f, 0.5f, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, true, false);
	Burn->InhibitAtMoisture = 0.5f;
	UDocMaterialReactionDefinition* Wet = DocMaterialTests::CreateWetting(World, WetTag, 1.0f);

	// Documented order: wetting-class reactions apply first, then ignition is evaluated against post-wetting moisture.
	// Heat and water arriving in the same step therefore never ignite, regardless of the order the sources arrived.
	auto RunBoundary = [&](bool bHeatFirst, bool& bOutIgnited, float& OutMoisture, int32& OutStarts)
	{
		UDocReactiveMaterialComponent* Comp = DocMaterialTests::SpawnReactive(World);
		Comp->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Log"), 100.0f, 0.0f, 0.0f, { Burn, Wet }));
		int32 Starts = 0;
		Comp->OnReactionStartedNative.AddLambda([&Starts](FName Id, const FDocReactionTransition&) { if (Id == TEXT("Burn")) { ++Starts; } });
		if (bHeatFirst)
		{
			Comp->AddExposureSource(DocMaterialTests::MakeSample(TEXT("Torch"), HeatTag, 2.0f));
			Comp->AddExposureSource(DocMaterialTests::MakeSample(TEXT("Hose"), WetTag, 1.0f));
		}
		else
		{
			Comp->AddExposureSource(DocMaterialTests::MakeSample(TEXT("Hose"), WetTag, 1.0f));
			Comp->AddExposureSource(DocMaterialTests::MakeSample(TEXT("Torch"), HeatTag, 2.0f));
		}
		Comp->StepSimulation(0.6f, 0.6);
		bOutIgnited = Comp->IsReactionActive(TEXT("Burn"));
		OutMoisture = Comp->CurrentState.Moisture;
		OutStarts = Starts;
	};

	bool bIgnitedA = true, bIgnitedB = true;
	float MoistureA = 0.0f, MoistureB = 0.0f;
	int32 StartsA = -1, StartsB = -1;
	RunBoundary(true, bIgnitedA, MoistureA, StartsA);
	RunBoundary(false, bIgnitedB, MoistureB, StartsB);
	TestFalse(TEXT("Same-step wetting wins over ignition (heat added first)"), bIgnitedA);
	TestFalse(TEXT("Same-step wetting wins over ignition (water added first)"), bIgnitedB);
	TestEqual(TEXT("No ignition event was emitted"), StartsA + StartsB, 0);
	TestEqual(TEXT("Moisture identical for either arrival order"), MoistureA, MoistureB);
	TestTrue(TEXT("Moisture was applied"), FMath::IsNearlyEqual(MoistureA, 0.6f, 1e-4f));

	// An established fire is put out by wetting with a reason code, and fuel stops being consumed.
	UDocReactiveMaterialComponent* Fire = DocMaterialTests::SpawnReactive(World);
	Fire->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Campfire"), 100.0f, 0.0f, 0.0f, { Burn, Wet }));
	Fire->AddExposureSource(DocMaterialTests::MakeSample(TEXT("Torch"), HeatTag, 2.0f));
	Fire->StepSimulation(0.5f, 0.5);
	TestTrue(TEXT("Fire active"), Fire->IsReactionActive(TEXT("Burn")));

	FDocReactionTransition EndT;
	Fire->OnReactionEndedNative.AddLambda([&EndT](FName, const FDocReactionTransition& T) { EndT = T; });
	Fire->AddExposureSource(DocMaterialTests::MakeSample(TEXT("Bucket"), WetTag, 1.0f));
	const float FuelBefore = Fire->CurrentState.Fuel;
	Fire->StepSimulation(0.6f, 1.1);
	TestFalse(TEXT("Wetting extinguishes the fire in the step it crosses the inhibit level"), Fire->IsReactionActive(TEXT("Burn")));
	TestEqual(TEXT("End state is Inhibited"), EndT.NewState, EDocReactionState::Inhibited);
	TestEqual(TEXT("End cause is MoistureInhibit"), EndT.Cause, FName(TEXT("MoistureInhibit")));
	TestEqual(TEXT("No fuel consumed in the extinguishing step"), Fire->CurrentState.Fuel, FuelBefore);

	// Manual extinguish still works and is reported.
	Fire->RemoveExposureSource(TEXT("Bucket"));
	Fire->CurrentState.Moisture = 0.0f;
	TestTrue(TEXT("Manual reignite"), Fire->RequestManualReaction(TEXT("Burn")));
	Fire->ExtinguishReactions(TEXT("WaterBucket"));
	TestFalse(TEXT("Manual extinguish"), Fire->IsReactionActive(TEXT("Burn")));
	TestEqual(TEXT("Manual extinguish cause"), EndT.Cause, FName(TEXT("WaterBucket")));

	return true;
}

// MAT-04: Phase and moisture remain within valid limits
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMaterialMeltAndDryBoundsTest, FAutomationTestBase, "Doc.Material.MeltAndDryBounds", DocMaterialTests::Flags)
bool FDocMaterialMeltAndDryBoundsTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	const FGameplayTag HeatTag = DocMaterialTests::GetOrCreateTestTag();
	const FGameplayTag WetTag = DocMaterialTests::GetOrCreateTestTag(TEXT("Doc.Test.Wet"));

	UDocReactiveMaterialComponent* Ice = DocMaterialTests::SpawnReactive(World);
	UDocMaterialReactionDefinition* MeltDef = DocMaterialTests::CreateTestReaction(World, TEXT("Melt"), NAME_None, 1, HeatTag,
		1.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.8f, 0.0f, false, true, false, 200.0f, true);
	Ice->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Ice"), 0.0f, 0.5f, 0.0f, { MeltDef }));
	Ice->RequestManualReaction(TEXT("Melt"));
	Ice->StepSimulation(2.0f, 2.0);
	TestEqual(TEXT("Phase fraction reaches and stays at 1.0"), Ice->CurrentState.PhaseFraction, 1.0f);
	TestEqual(TEXT("Moisture reaches 0.0"), Ice->CurrentState.Moisture, 0.0f);
	FDocReactionInstance MeltInst;
	Ice->GetReactionInstance(TEXT("Melt"), MeltInst);
	TestTrue(TEXT("Reported phase delta is the clamped amount"), FMath::IsNearlyEqual(MeltInst.DeltaPhase, 1.0f, 1e-4f));

	// Drying consumes existing moisture only and stops at zero.
	UDocReactiveMaterialComponent* Cloth = DocMaterialTests::SpawnReactive(World);
	UDocMaterialReactionDefinition* Dry = DocMaterialTests::CreateTestReaction(World, TEXT("Dry"), NAME_None, 1, HeatTag,
		0.5f, 0.2f, 0.0f, 0.0f, 0.4f, 0.0f, 0.0f, false, true);
	Cloth->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Cloth"), 0.0f, 0.3f, 0.0f, { Dry }));
	Cloth->AddExposureSource(DocMaterialTests::MakeSample(TEXT("Sun"), HeatTag, 1.0f));
	Cloth->StepSimulation(0.5f, 0.5);
	TestTrue(TEXT("Drying reduced moisture"), FMath::IsNearlyEqual(Cloth->CurrentState.Moisture, 0.1f, 1e-4f));
	Cloth->StepSimulation(0.5f, 1.0);
	TestEqual(TEXT("Drying stops at zero"), Cloth->CurrentState.Moisture, 0.0f);
	TestFalse(TEXT("Drying ends when no moisture is left"), Cloth->IsReactionActive(TEXT("Dry")));
	Cloth->StepSimulation(0.5f, 1.5);
	TestEqual(TEXT("Drying never creates negative moisture"), Cloth->CurrentState.Moisture, 0.0f);

	// Wetting caps at 1.
	UDocReactiveMaterialComponent* Sponge = DocMaterialTests::SpawnReactive(World);
	Sponge->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Sponge"), 0.0f, 0.5f, 0.0f, { DocMaterialTests::CreateWetting(World, WetTag, 5.0f) }));
	Sponge->AddExposureSource(DocMaterialTests::MakeSample(TEXT("Rain"), WetTag, 1.0f));
	Sponge->StepSimulation(1.0f, 1.0);
	TestEqual(TEXT("Moisture capped at 1"), Sponge->CurrentState.Moisture, 1.0f);

	// Profile validation refuses out-of-range and contradictory authoring.
	UDocMaterialProfile* Bad = DocMaterialTests::CreateTestProfile(World, TEXT("Bad"), 10.0f, 1.5f, 0.0f, {});
	FString Error;
	TestFalse(TEXT("Moisture above 1 refused"), Bad->ValidateProfile(Error));
	UDocMaterialReactionDefinition* Both = DocMaterialTests::CreateWetting(World, WetTag, 1.0f);
	Both->MoistureConsumptionRate = 1.0f;
	UDocMaterialProfile* Contradictory = DocMaterialTests::CreateTestProfile(World, TEXT("Contra"), 10.0f, 0.0f, 0.0f, { Both });
	TestFalse(TEXT("Reaction that both adds and consumes moisture refused"), Contradictory->ValidateProfile(Error));

	return true;
}

// MAT-05: Removing one exposure/suppression source leaves others intact
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMaterialSourceOwnershipTest, FAutomationTestBase, "Doc.Material.SourceOwnership", DocMaterialTests::Flags)
bool FDocMaterialSourceOwnershipTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	UDocReactiveMaterialComponent* Comp = DocMaterialTests::SpawnReactive(World);
	const FGameplayTag HeatTag = DocMaterialTests::GetOrCreateTestTag();

	UDocMaterialReactionDefinition* BurnDef = DocMaterialTests::CreateTestReaction(World, TEXT("Burn"), NAME_None, 1, HeatTag,
		1.0f, 0.5f, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, true, false, false, 200.0f, true);
	UDocMaterialProfile* Profile = DocMaterialTests::CreateTestProfile(World, TEXT("Log"), 100.0f, 0.0f, 0.0f, { BurnDef });
	Comp->InitializeFromProfile(Profile);

	Comp->AddExposureSource(DocMaterialTests::MakeSample(TEXT("TorchA"), HeatTag, 0.6f));
	Comp->AddExposureSource(DocMaterialTests::MakeSample(TEXT("TorchB"), HeatTag, 0.7f, 5));
	TestTrue(TEXT("Aggregated heat is sum"), FMath::IsNearlyEqual(Comp->GetAggregatedChannelIntensity(HeatTag), 1.3f, 1e-4f));

	// Stale updates from a source are refused; newer ones replace only that source.
	TestFalse(TEXT("Older sequence refused"), Comp->AddExposureSource(DocMaterialTests::MakeSample(TEXT("TorchB"), HeatTag, 5.0f, 4)));
	TestEqual(TEXT("Stale reason"), Comp->GetLastRejectReason(), FName(TEXT("StaleSequence")));
	TestTrue(TEXT("Newer sequence accepted"), Comp->AddExposureSource(DocMaterialTests::MakeSample(TEXT("TorchB"), HeatTag, 0.8f, 6)));
	TestTrue(TEXT("TorchA untouched by TorchB update"), FMath::IsNearlyEqual(Comp->GetAggregatedChannelIntensity(HeatTag), 1.4f, 1e-4f));

	Comp->RemoveExposureSource(TEXT("TorchA"));
	TestTrue(TEXT("Remaining heat from TorchB is intact"), FMath::IsNearlyEqual(Comp->GetAggregatedChannelIntensity(HeatTag), 0.8f, 1e-4f));

	// Inputs are capped before evaluation.
	Profile->DefaultChannelMaxValue = 3.0f;
	Comp->AddExposureSource(DocMaterialTests::MakeSample(TEXT("Inferno"), HeatTag, 1000.0f));
	TestEqual(TEXT("Combined channel capped"), Comp->GetAggregatedChannelIntensity(HeatTag), 3.0f);
	Comp->RemoveExposureSource(TEXT("Inferno"));

	// Weighted rule is a weighted mean, not a silent sum.
	Profile->ChannelRule = EDocChannelCombinationRule::Weighted;
	FDocExposureSample Heavy = DocMaterialTests::MakeSample(TEXT("Heavy"), HeatTag, 2.0f);
	Heavy.Weight = 3.0f;
	Comp->AddExposureSource(Heavy); // TorchB 0.8 * 1 + 2.0 * 3 = 6.8 over 4
	TestTrue(TEXT("Weighted mean"), FMath::IsNearlyEqual(Comp->GetAggregatedChannelIntensity(HeatTag), 1.7f, 1e-4f));
	Profile->ChannelRule = EDocChannelCombinationRule::Sum;

	// A temporary weather suppression cannot remove a script's permanent inhibit.
	Comp->SuppressReaction(TEXT("Burn"), TEXT("WeatherSource"), 1.0f);
	Comp->SuppressReaction(TEXT("Burn"), TEXT("ScriptSource"));
	TestTrue(TEXT("Suppressed by both"), Comp->IsReactionSuppressed(TEXT("Burn")));
	Comp->StepSimulation(1.5f, 1.5);
	TestTrue(TEXT("Script inhibit survives weather expiry"), Comp->IsReactionSuppressed(TEXT("Burn")));
	TestFalse(TEXT("Suppressed reaction does not start"), Comp->IsReactionActive(TEXT("Burn")));
	Comp->UnsuppressReaction(TEXT("Burn"), TEXT("WeatherSource"));
	TestTrue(TEXT("Removing an expired weather entry leaves the script entry"), Comp->IsReactionSuppressed(TEXT("Burn")));
	Comp->UnsuppressReaction(TEXT("Burn"), TEXT("ScriptSource"));
	TestFalse(TEXT("Fully unsuppressed"), Comp->IsReactionSuppressed(TEXT("Burn")));

	// Suppressing an active reaction ends it with a reason.
	Comp->RequestManualReaction(TEXT("Burn"));
	FDocReactionTransition EndT;
	Comp->OnReactionEndedNative.AddLambda([&EndT](FName, const FDocReactionTransition& T) { EndT = T; });
	Comp->SuppressReaction(TEXT("Burn"), TEXT("ScriptSource"));
	TestFalse(TEXT("Suppression ends active reaction"), Comp->IsReactionActive(TEXT("Burn")));
	TestEqual(TEXT("End state Suppressed"), EndT.NewState, EDocReactionState::Suppressed);

	return true;
}

// MAT-06: Exclusive reactions cannot double-consume the same resource
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMaterialCompetingReactionsTest, FAutomationTestBase, "Doc.Material.CompetingReactions", DocMaterialTests::Flags)
bool FDocMaterialCompetingReactionsTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	UDocReactiveMaterialComponent* Comp = DocMaterialTests::SpawnReactive(World);
	const FGameplayTag HeatTag = DocMaterialTests::GetOrCreateTestTag();

	UDocMaterialReactionDefinition* FastBurn = DocMaterialTests::CreateTestReaction(World, TEXT("FastBurn"), TEXT("ThermalConsumption"), 10,
		HeatTag, 1.0f, 0.5f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f, true, false);
	UDocMaterialReactionDefinition* SlowSmolder = DocMaterialTests::CreateTestReaction(World, TEXT("SlowSmolder"), TEXT("ThermalConsumption"), 5,
		HeatTag, 1.0f, 0.5f, 0.0f, 5.0f, 0.0f, 0.0f, 0.0f, true, false);

	// Authoring order deliberately lists the lower priority first; evaluation order must not depend on it.
	Comp->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("FuelBlock"), 100.0f, 0.0f, 0.0f, { SlowSmolder, FastBurn }));

	TArray<FName> Started;
	Comp->OnReactionStartedNative.AddLambda([&Started](FName Id, const FDocReactionTransition&) { Started.Add(Id); });

	Comp->AddExposureSource(DocMaterialTests::MakeSample(TEXT("HeatSource"), HeatTag, 2.0f));
	Comp->StepSimulation(1.0f, 1.0);

	TestEqual(TEXT("Exclusive group prevents double consumption"), Comp->CurrentState.Fuel, 80.0f);
	TestEqual(TEXT("Only the winner started"), Started.Num(), 1);
	TestTrue(TEXT("Winner is the higher priority"), Started.Num() == 1 && Started[0] == TEXT("FastBurn"));
	FDocReactionInstance Loser;
	Comp->GetReactionInstance(TEXT("SlowSmolder"), Loser);
	TestEqual(TEXT("Loser reports Outcompeted"), Loser.State, EDocReactionState::Outcompeted);

	Comp->StepSimulation(1.0f, 2.0);
	TestEqual(TEXT("Still single consumer on the next step"), Comp->CurrentState.Fuel, 60.0f);

	// Manual request cannot steal the group from a higher priority reaction.
	TestFalse(TEXT("Lower priority manual start refused"), Comp->RequestManualReaction(TEXT("SlowSmolder")));
	TestEqual(TEXT("Reason GroupOccupied"), Comp->GetLastRejectReason(), FName(TEXT("GroupOccupied")));

	// When the winner stops (suppressed), the other reaction takes over without double consumption.
	Comp->SuppressReaction(TEXT("FastBurn"), TEXT("Script"));
	Comp->StepSimulation(1.0f, 3.0);
	TestTrue(TEXT("Smolder took over"), Comp->IsReactionActive(TEXT("SlowSmolder")));
	TestEqual(TEXT("Only smolder consumed"), Comp->CurrentState.Fuel, 55.0f);

	return true;
}

// MAT-07: Contacts deduplicate and spread remains within explicit work limits
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMaterialPropagationBudgetTest, FAutomationTestBase, "Doc.Material.PropagationBudget", DocMaterialTests::Flags)
bool FDocMaterialPropagationBudgetTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	UDocMaterialReactionSubsystem* Subsystem = World->GetSubsystem<UDocMaterialReactionSubsystem>();
	TestNotNull(TEXT("Subsystem exists"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}
	const FGameplayTag HeatTag = DocMaterialTests::GetOrCreateTestTag();

	UDocMaterialReactionDefinition* PropBurn = DocMaterialTests::CreateTestReaction(World, TEXT("PropBurn"), NAME_None, 1,
		HeatTag, 1.0f, 0.5f, 0.0f, 5.0f, 0.0f, 0.0f, 0.0f, true, false, true, 300.0f, true);

	UDocReactiveMaterialComponent* Source = DocMaterialTests::SpawnReactive(World, FVector::ZeroVector, TEXT("Src"));
	Source->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("SourceLog"), 100.0f, 0.0f, 0.0f, { PropBurn }));
	Source->RequestManualReaction(TEXT("PropBurn"));

	// Inert targets (no reactions) so they never become sources themselves.
	TArray<UDocReactiveMaterialComponent*> Targets;
	for (int32 i = 0; i < 10; ++i)
	{
		UDocReactiveMaterialComponent* T = DocMaterialTests::SpawnReactive(World, FVector(100.0f + i * 10.0f, 0, 0), *FString::Printf(TEXT("T%02d"), i));
		T->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Stone"), 0.0f, 0.0f, 0.0f, {}));
		Targets.Add(T);
	}

	FDocPropagationBudget Budget;
	Budget.MaxNeighborsPerSource = 3;
	Budget.MaxWorkPerStep = 10;
	Subsystem->SetPropagationBudget(Budget);

	Subsystem->StepSimulation(0.5f);
	auto CountExposed = [&]()
	{
		int32 N = 0;
		for (UDocReactiveMaterialComponent* T : Targets)
		{
			N += T->GetAggregatedChannelIntensity(HeatTag) > 0.0f ? 1 : 0;
		}
		return N;
	};
	TestEqual(TEXT("Neighbor limit of 3 respected"), CountExposed(), 3);
	TestTrue(TEXT("Nearest neighbors chosen"), Targets[0]->GetExposureSourceCount() == 1 && Targets[2]->GetExposureSourceCount() == 1 && Targets[3]->GetExposureSourceCount() == 0);
	FDocPropagationStats Stats = Subsystem->GetLastPropagationStats();
	TestEqual(TEXT("Transfers issued"), Stats.TransfersIssued, 3);
	TestTrue(TEXT("Work stayed within the limit"), Stats.CandidatesEvaluated <= 10);

	// Repeated steps refresh the same transfer instead of stacking samples.
	Subsystem->StepSimulation(0.5f);
	Subsystem->StepSimulation(0.5f);
	TestEqual(TEXT("Still one sample per target"), Targets[0]->GetExposureSourceCount(), 1);
	TestEqual(TEXT("Intensity not stacked"), Targets[0]->GetAggregatedChannelIntensity(HeatTag), 1.0f);

	// Contacts deduplicate: adding the same logical contact twice is NoChange, and it is not also counted as a spatial neighbor.
	TestTrue(TEXT("Contact added"), Subsystem->AddContact(TEXT("Src"), TEXT("T09")).IsChanged());
	const FDocSystemResult Again = Subsystem->AddContact(TEXT("T09"), TEXT("Src"));
	TestTrue(TEXT("Duplicate contact is NoChange"), Again.IsSuccess() && !Again.IsChanged());
	TestEqual(TEXT("One contact stored"), Subsystem->GetContactCount(), 1);
	// Two steps: the displaced nearest-neighbor sample lives until its expiry (two steps), then drops.
	Subsystem->StepSimulation(0.5f);
	Subsystem->StepSimulation(0.5f);
	TestEqual(TEXT("Contacted target has a single sample"), Targets[9]->GetExposureSourceCount(), 1);
	TestEqual(TEXT("Contact takes a neighbor slot, total stays at 3"), CountExposed(), 3);
	TestEqual(TEXT("Displaced neighbor expired"), Targets[2]->GetExposureSourceCount(), 0);

	// Tight work budget defers instead of overrunning.
	Budget.MaxWorkPerStep = 4;
	Subsystem->SetPropagationBudget(Budget);
	Subsystem->StepSimulation(0.5f);
	Stats = Subsystem->GetLastPropagationStats();
	TestEqual(TEXT("Candidate checks capped at the work budget"), Stats.CandidatesEvaluated, 4);
	TestEqual(TEXT("Source reported as deferred"), Stats.SourcesDeferred, 1);

	// Generation limit: A -> B allowed, B -> C refused at MaxPropagationGeneration = 1.
	UDocMaterialReactionDefinition* Limited = DocMaterialTests::CreateTestReaction(World, TEXT("Spread"), NAME_None, 1,
		HeatTag, 1.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, true, false, true, 250.0f, true);
	Limited->MaxPropagationGeneration = 1;
	UDocReactiveMaterialComponent* A = DocMaterialTests::SpawnReactive(World, FVector(0, 5000, 0), TEXT("GenA"));
	UDocReactiveMaterialComponent* B = DocMaterialTests::SpawnReactive(World, FVector(200, 5000, 0), TEXT("GenB"));
	UDocReactiveMaterialComponent* C = DocMaterialTests::SpawnReactive(World, FVector(400, 5000, 0), TEXT("GenC"));
	A->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Hay"), 100.0f, 0.0f, 0.0f, { Limited }));
	B->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Hay"), 100.0f, 0.0f, 0.0f, { Limited }));
	C->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Hay"), 100.0f, 0.0f, 0.0f, { Limited }));
	Budget.MaxWorkPerStep = 256;
	Budget.MaxNeighborsPerSource = 8;
	Subsystem->SetPropagationBudget(Budget);
	A->RequestManualReaction(TEXT("Spread"));
	for (int32 i = 0; i < 4; ++i)
	{
		Subsystem->StepSimulation(0.5f);
	}
	TestTrue(TEXT("B ignited from A"), B->IsReactionActive(TEXT("Spread")));
	FDocReactionInstance BInst;
	B->GetReactionInstance(TEXT("Spread"), BInst);
	TestEqual(TEXT("B is generation 1"), BInst.Generation, 1);
	TestFalse(TEXT("C not reached past the generation limit"), C->IsReactionActive(TEXT("Spread")));
	TestTrue(TEXT("Generation refusals are counted"), Subsystem->GetLastPropagationStats().TransfersRejectedGeneration > 0);

	return true;
}

// MAT-08: Unloaded targets are not mutated through stale actor references
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMaterialUnloadedTargetTest, FAutomationTestBase, "Doc.Material.UnloadedTarget", DocMaterialTests::Flags)
bool FDocMaterialUnloadedTargetTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	UDocMaterialReactionSubsystem* Subsystem = World->GetSubsystem<UDocMaterialReactionSubsystem>();
	const FGameplayTag HeatTag = DocMaterialTests::GetOrCreateTestTag();

	UDocMaterialReactionDefinition* PropBurn = DocMaterialTests::CreateTestReaction(World, TEXT("PropBurn"), NAME_None, 1,
		HeatTag, 1.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, true, false, true, 300.0f, true);

	UDocReactiveMaterialComponent* Source = DocMaterialTests::SpawnReactive(World, FVector::ZeroVector, TEXT("Src"));
	Source->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Log"), 100.0f, 0.0f, 0.0f, { PropBurn }));
	UDocReactiveMaterialComponent* Target = DocMaterialTests::SpawnReactive(World, FVector(100, 0, 0), TEXT("Tgt"));
	Target->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Stone"), 0.0f, 0.0f, 0.0f, {}));
	TestEqual(TEXT("Two registered"), Subsystem->GetRegisteredComponentCount(), 2);

	Source->RequestManualReaction(TEXT("PropBurn"));
	Subsystem->StepSimulation(0.1f);
	TestEqual(TEXT("Target received propagated exposure"), Target->GetExposureSourceCount(), 1);

	// Unloading the source withdraws its contribution immediately.
	Source->GetOwner()->Destroy();
	TestEqual(TEXT("Unloaded source's exposure withdrawn"), Target->GetExposureSourceCount(), 0);
	Subsystem->StepSimulation(0.1f);
	TestEqual(TEXT("Stale source cleaned up"), Subsystem->GetRegisteredComponentCount(), 1);

	// Unloading the target: id-based API reports NotFound and nothing is mutated.
	const FDocMaterialState Before = Target->CurrentState;
	Target->GetOwner()->Destroy();
	Subsystem->StepSimulation(0.1f);
	TestEqual(TEXT("Stale target cleaned up"), Subsystem->GetRegisteredComponentCount(), 0);
	TestEqual(TEXT("Request to unloaded object is NotFound"), Subsystem->RequestReaction(TEXT("Tgt"), TEXT("PropBurn"), NAME_None).Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("Exposure to unloaded object is NotFound"),
		Subsystem->AddExposureSource(TEXT("Tgt"), DocMaterialTests::MakeSample(TEXT("Torch"), HeatTag, 5.0f)).Outcome, EDocResultOutcome::NotFound);
	FDocMaterialState Queried;
	TestFalse(TEXT("Query of unloaded object fails"), Subsystem->QueryMaterialState(TEXT("Tgt"), Queried));
	TestFalse(TEXT("Direct call on the unregistered component is refused"),
		Target->AddExposureSource(DocMaterialTests::MakeSample(TEXT("Torch"), HeatTag, 5.0f)));
	TestEqual(TEXT("Stale component facts unchanged"), Target->CurrentState.Revision, Before.Revision);
	TestFalse(TEXT("Contact to unloaded object refused"), Subsystem->AddContact(TEXT("Src"), TEXT("Tgt")).IsSuccess());

	return true;
}

// MAT-09: Restored burning state rebuilds visuals without new gameplay effects
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMaterialRestoreNoIgnitionEffectsTest, FAutomationTestBase, "Doc.Material.RestoreNoIgnitionEffects", DocMaterialTests::Flags)
bool FDocMaterialRestoreNoIgnitionEffectsTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	UDocMaterialReactionSubsystem* Subsystem = World->GetSubsystem<UDocMaterialReactionSubsystem>();
	UDocReactiveMaterialComponent* Comp = DocMaterialTests::SpawnReactive(World);
	const FGameplayTag HeatTag = DocMaterialTests::GetOrCreateTestTag();
	const FGameplayTag BurningTag = DocMaterialTests::GetOrCreateTestTag(TEXT("Doc.Test.Burning"));

	UDocMaterialReactionDefinition* BurnDef = DocMaterialTests::CreateTestReaction(World, TEXT("Burn"), NAME_None, 1, HeatTag,
		1.0f, 0.5f, 0.0f, 10.0f, 0.0f, 0.0f, 0.1f, true, false, false, 200.0f, true);
	BurnDef->ResultingTags.AddTag(BurningTag);
	Comp->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Wood"), 100.0f, 0.0f, 0.0f, { BurnDef }));

	Comp->RequestManualReaction(TEXT("Burn"));
	Comp->StepSimulation(2.0f, 2.0);
	Comp->SuppressReaction(TEXT("Burn"), TEXT("TimedScript"), 30.0f);
	Comp->UnsuppressReaction(TEXT("Burn"), TEXT("TimedScript"));
	TestEqual(TEXT("Fuel after burning"), Comp->CurrentState.Fuel, 80.0f);

	// Burning again for the snapshot.
	Comp->RequestManualReaction(TEXT("Burn"));
	FDocReactionInstance LiveInst;
	Comp->GetReactionInstance(TEXT("Burn"), LiveInst);

	FDocMaterialSnapshot Saved;
	TestTrue(TEXT("State captured"), Subsystem->CaptureMaterialState(Comp, Saved));
	TestEqual(TEXT("Captured fuel"), Saved.State.Fuel, 80.0f);
	TestEqual(TEXT("Captured one live reaction"), Saved.Reactions.Num(), 1);

	// Diverge, then restore.
	Comp->ExtinguishReactions(TEXT("Diverge"));
	Comp->StepSimulation(1.0f, 3.0);

	int32 StartEvents = 0;
	TArray<FName> Causes;
	Comp->OnReactionStartedNative.AddLambda([&StartEvents](FName, const FDocReactionTransition&) { ++StartEvents; });
	Comp->OnMaterialStateChangedNative.AddLambda([&Causes](const FDocMaterialState&, FName Cause) { Causes.Add(Cause); });

	const FDocSystemResult Restored = Subsystem->StageRestore(Comp, Saved);
	TestTrue(TEXT("Restore succeeded"), Restored.IsSuccess());
	TestEqual(TEXT("Restored fuel is exactly the saved fuel"), Comp->CurrentState.Fuel, 80.0f);
	TestTrue(TEXT("Burning restored as active"), Comp->IsReactionActive(TEXT("Burn")));
	TestTrue(TEXT("Burning tag rebuilt from state"), Comp->CurrentState.StateTags.HasTagExact(BurningTag));
	FDocReactionInstance RestoredInst;
	Comp->GetReactionInstance(TEXT("Burn"), RestoredInst);
	TestEqual(TEXT("Same transition id (not a new ignition)"), RestoredInst.TransitionId, LiveInst.TransitionId);
	TestEqual(TEXT("Same receipt id (effects not re-issued)"), RestoredInst.ReceiptId, LiveInst.ReceiptId);
	TestEqual(TEXT("No ignition events on restore"), StartEvents, 0);
	TestTrue(TEXT("One presentation rebuild event"), Causes.Num() == 1 && Causes[0] == TEXT("Restored"));

	// The restored reaction continues from saved progress.
	Comp->StepSimulation(1.0f, 4.0);
	TestEqual(TEXT("Consumption resumes from saved fuel"), Comp->CurrentState.Fuel, 70.0f);
	TestEqual(TEXT("Still no ignition events"), StartEvents, 0);

	// Invalid snapshots are refused without touching state.
	const FDocMaterialState BeforeBad = Comp->CurrentState;
	FDocMaterialSnapshot Bad = Saved;
	Bad.State.Fuel = 500.0f;
	TestFalse(TEXT("Fuel above max refused"), Subsystem->StageRestore(Comp, Bad).IsSuccess());
	Bad = Saved;
	Bad.ContentVersion = 99;
	TestFalse(TEXT("Content version mismatch refused"), Subsystem->StageRestore(Comp, Bad).IsSuccess());
	Bad = Saved;
	const FDocReactionInstance DuplicateInst = Bad.Reactions[0];
	Bad.Reactions.Add(DuplicateInst);
	TestFalse(TEXT("Duplicate reaction refused"), Subsystem->StageRestore(Comp, Bad).IsSuccess());
	TestEqual(TEXT("State untouched by refused restores"), Comp->CurrentState.Revision, BeforeBad.Revision);
	TestEqual(TEXT("Fuel untouched by refused restores"), Comp->CurrentState.Fuel, BeforeBad.Fuel);

	return true;
}

// MAT-10: Base runs without Weather, Fluid, Niagara, or Surface Feedback
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocMaterialAdapterAbsenceTest, FAutomationTestBase, "Doc.Material.AdapterAbsence", DocMaterialTests::Flags)
bool FDocMaterialAdapterAbsenceTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	UDocMaterialReactionSubsystem* Subsystem = World->GetSubsystem<UDocMaterialReactionSubsystem>();
	TestNotNull(TEXT("Subsystem instantiates cleanly in isolation"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}
	const FGameplayTag HeatTag = DocMaterialTests::GetOrCreateTestTag();

	// A full ignite / burn / query cycle through the id API with no bridge plugins present.
	UDocMaterialReactionDefinition* BurnDef = DocMaterialTests::CreateTestReaction(World, TEXT("Burn"), NAME_None, 1, HeatTag,
		1.0f, 0.5f, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, true, false, false, 200.0f, true);
	UDocReactiveMaterialComponent* Crate = DocMaterialTests::SpawnReactive(World, FVector::ZeroVector, TEXT("Crate"));
	Crate->InitializeFromProfile(DocMaterialTests::CreateTestProfile(World, TEXT("Crate"), 50.0f, 0.0f, 0.0f, { BurnDef }));

	TestTrue(TEXT("RequestReaction by id"), Subsystem->RequestReaction(TEXT("Crate"), TEXT("Burn"), TEXT("Script")).IsChanged());
	const FDocSystemResult Repeat = Subsystem->RequestReaction(TEXT("Crate"), TEXT("Burn"), TEXT("Script"));
	TestTrue(TEXT("Repeat request is NoChange"), Repeat.IsSuccess() && !Repeat.IsChanged());
	TestEqual(TEXT("Unknown reaction is NotFound"), Subsystem->RequestReaction(TEXT("Crate"), TEXT("Freeze"), NAME_None).Outcome, EDocResultOutcome::NotFound);

	// Fixed-step driver: 0.35 s at 0.1 s steps runs 3 steps and carries the remainder.
	Subsystem->FixedStepSeconds = 0.1f;
	Subsystem->MaxStepsPerTick = 4;
	Subsystem->Tick(0.35f);
	TestTrue(TEXT("Three fixed steps ran"), FMath::IsNearlyEqual(Subsystem->GetSimulationTime(), 0.3, 1e-4));
	FDocMaterialState State;
	TestTrue(TEXT("QueryMaterialState by id"), Subsystem->QueryMaterialState(TEXT("Crate"), State));
	TestTrue(TEXT("Fuel consumed over three steps"), FMath::IsNearlyEqual(State.Fuel, 47.0f, 1e-3f));
	TestEqual(TEXT("QueryActiveReactions by id"), Subsystem->QueryActiveReactions(TEXT("Crate")).Num(), 1);

	// A long frame is bounded: at most MaxStepsPerTick steps, the rest is dropped and reported.
	Subsystem->Tick(1.0f);
	TestTrue(TEXT("Bounded catch-up"), FMath::IsNearlyEqual(Subsystem->GetSimulationTime(), 0.7, 1e-4));
	TestTrue(TEXT("Dropped time is visible"), Subsystem->GetDroppedSimulationSeconds() > 0.5);

	// Duplicate ids are refused rather than silently aliased.
	UDocReactiveMaterialComponent* Clash = DocMaterialTests::SpawnReactive(World, FVector::ZeroVector, TEXT("Crate"));
	TestTrue(TEXT("Clashing id not registered"), Subsystem->FindReactiveObject(TEXT("Crate")) == Crate);
	TestEqual(TEXT("Registration conflict reported"), Subsystem->RegisterReactiveObject(Clash).Outcome, EDocResultOutcome::Conflict);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
