#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "DocModContentTypes.h"
#include "DocModDefinitionProvider.h"
#include "DocModContentSubsystem.h"
#include "DocModContentSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helper to create a temporary directory for testing mod packs
struct FDocScopedTestModDir
{
	FString RootDir;

	FDocScopedTestModDir(const FString& Name)
	{
		RootDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestMods"), Name);
		IFileManager::Get().MakeDirectory(*RootDir, true);
	}

	~FDocScopedTestModDir()
	{
		IFileManager::Get().DeleteDirectory(*RootDir, false, true);
	}

	void WriteFile(const FString& RelativePath, const FString& Content)
	{
		FString FullPath = FPaths::Combine(RootDir, RelativePath);
		FString Dir = FPaths::GetPath(FullPath);
		IFileManager::Get().MakeDirectory(*Dir, true);
		FFileHelper::SaveStringToFile(Content, *FullPath);
	}
};

namespace DocModTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FFixture
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UDocModContentSubsystem> Subsystem;

		FFixture()
		{
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			Subsystem.Reset(NewObject<UDocModContentSubsystem>(GameInstance.Get()));
			UDocModContentSubsystem::SetSubsystemOverrideForTesting(Subsystem.Get());
		}

		~FFixture()
		{
			UDocModContentSubsystem::SetSubsystemOverrideForTesting(nullptr);
		}

		UDocModContentSubsystem* Get() const { return Subsystem.Get(); }
	};
}


// MOD-01: Manifest/schema/version/range validation is strict and bounded
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocModManifestSchemaTest, "Doc.Mod.ManifestSchema", DocModTests::Flags)

bool FDocModManifestSchemaTest::RunTest(const FString& Parameters)
{
	// 1. Valid manifest JSON
	FString ValidJson = TEXT(R"({
		"manifest_version": 1,
		"pack_id": "test.author.pack_a",
		"pack_version": "1.2.3",
		"display_name": "Pack A",
		"requires": [
			{ "pack_id": "base.pack", "version": ">=1.0.0" }
		],
		"conflicts": ["incompatible.pack"],
		"definitions": [
			{
				"id": "test.author.pack_a:def_1",
				"schema": "doc.sample.definition",
				"schema_version": 1,
				"path": "defs/def_1.json"
			}
		]
	})");

	FDocModManifest Manifest;
	FString Error;
	TestTrue(TEXT("Valid manifest parses successfully"), FDocModManifest::ParseFromJson(ValidJson, Manifest, Error));
	TestEqual(TEXT("Manifest version"), Manifest.ManifestVersion, 1);
	TestEqual(TEXT("Pack ID"), Manifest.PackId, TEXT("test.author.pack_a"));
	TestEqual(TEXT("Pack Version"), Manifest.PackVersion.ToString(), TEXT("1.2.3"));
	TestEqual(TEXT("Requires count"), Manifest.Requires.Num(), 1);
	TestEqual(TEXT("Requires pack ID"), Manifest.Requires[0].PackId, TEXT("base.pack"));
	TestEqual(TEXT("Requires constraint"), Manifest.Requires[0].VersionConstraint, TEXT(">=1.0.0"));
	TestEqual(TEXT("Conflicts count"), Manifest.Conflicts.Num(), 1);
	TestEqual(TEXT("Definitions count"), Manifest.Definitions.Num(), 1);

	// 2. Malformed JSON
	FString MalformedJson = TEXT("{ invalid json format }");
	FDocModManifest BadManifest;
	TestFalse(TEXT("Malformed JSON fails"), FDocModManifest::ParseFromJson(MalformedJson, BadManifest, Error));

	// 3. Missing pack_id
	FString MissingPackIdJson = TEXT(R"({
		"manifest_version": 1,
		"pack_version": "1.0.0"
	})");
	TestFalse(TEXT("Missing pack_id fails"), FDocModManifest::ParseFromJson(MissingPackIdJson, BadManifest, Error));

	// 4. Semver constraint parsing
	FDocModSemVer Ver(1, 2, 4);
	TestTrue(TEXT(">=1.0.0 matches 1.2.4"), Ver.MatchesConstraint(TEXT(">=1.0.0")));
	TestTrue(TEXT("<=2.0.0 matches 1.2.4"), Ver.MatchesConstraint(TEXT("<=2.0.0")));
	TestTrue(TEXT("^1.0.0 matches 1.2.4"), Ver.MatchesConstraint(TEXT("^1.0.0")));
	TestFalse(TEXT("^2.0.0 rejects 1.2.4"), Ver.MatchesConstraint(TEXT("^2.0.0")));
	TestFalse(TEXT(">=2.0.0 rejects 1.2.4"), Ver.MatchesConstraint(TEXT(">=2.0.0")));

	// 5. Unsupported manifest_version, file-count quota, schema_version, oversized manifest
	DocModTests::FFixture Fixture;
	UDocModContentSubsystem* Subsystem = Fixture.Get();
	Subsystem->RegisterProviderObject(NewObject<UDocModSampleDefinitionProvider>());
	FDocModManifest Future = Manifest;
	Future.ManifestVersion = 99;
	TestEqual(TEXT("Future manifest_version is rejected"), Subsystem->ValidateManifest(Future, Error), EDocModValidationResult::InvalidVersion);

	UDocModContentSettings* Settings = GetMutableDefault<UDocModContentSettings>();
	const int32 SavedMaxFiles = Settings->MaxFilesPerPack;
	Settings->MaxFilesPerPack = 1;
	FDocModManifest TooMany = Manifest;
	TooMany.Definitions.Add(Manifest.Definitions[0]);
	TooMany.Definitions[1].DefinitionId = TEXT("test.author.pack_a:def_2");
	TestEqual(TEXT("File-count quota enforced"), Subsystem->ValidateManifest(TooMany, Error), EDocModValidationResult::ResourceLimitExceeded);
	Settings->MaxFilesPerPack = SavedMaxFiles;

	FDocScopedTestModDir SchemaDir(TEXT("SchemaVersionTest"));
	SchemaDir.WriteFile(TEXT("defs/def_1.json"), TEXT("{}"));
	FDocModManifest NewerSchema = Manifest;
	NewerSchema.PackDirectoryPath = SchemaDir.RootDir;
	NewerSchema.Definitions[0].SchemaVersion = 2;
	TMap<FString, FString> Hashes;
	TestEqual(TEXT("Newer schema_version than the provider supports is rejected"),
		Subsystem->ValidatePackContent(NewerSchema, SchemaDir.RootDir, Error, Hashes), EDocModValidationResult::UnsupportedSchema);

	FDocScopedTestModDir BigDir(TEXT("BigManifestTest"));
	BigDir.WriteFile(TEXT("manifest.json"), FString::ChrN(static_cast<int32>(Settings->MaxManifestBytes) + 16, TEXT(' ')));
	FDocModManifest Big;
	TestFalse(TEXT("Oversized manifest.json is refused before parsing"), Subsystem->DiscoverPackInDirectory(BigDir.RootDir, Big, Error));

	return true;
}

// MOD-02: Traversal, absolute paths, links/reparse escapes, and case collisions fail
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocModPathContainmentTest, "Doc.Mod.PathContainment",
	DocModTests::Flags)

bool FDocModPathContainmentTest::RunTest(const FString& Parameters)
{
	DocModTests::FFixture Fixture;
	UDocModContentSubsystem* Subsystem = Fixture.Get();
	FString PackRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestMods"), TEXT("PathTestPack"));
	IFileManager::Get().MakeDirectory(*PackRoot, true);

	FString Canonical;
	FString Error;

	// 1. Path traversal via '..'
	EDocModValidationResult Res1 = Subsystem->ValidatePathSafety(PackRoot, TEXT("../escaped.json"), Canonical, Error);
	TestEqual(TEXT("Relative path with .. is rejected"), Res1, EDocModValidationResult::PathTraversal);

	// 2. Absolute Windows path with drive letter
	EDocModValidationResult Res2 = Subsystem->ValidatePathSafety(PackRoot, TEXT("C:/Windows/System32/evil.json"), Canonical, Error);
	TestEqual(TEXT("Absolute path with drive letter is rejected"), Res2, EDocModValidationResult::PathTraversal);

	// 3. Absolute path with leading slash
	EDocModValidationResult Res3 = Subsystem->ValidatePathSafety(PackRoot, TEXT("/root/escape.json"), Canonical, Error);
	TestEqual(TEXT("Absolute path with leading slash is rejected"), Res3, EDocModValidationResult::PathTraversal);

	// 4. Valid nested path inside pack root
	EDocModValidationResult Res4 = Subsystem->ValidatePathSafety(PackRoot, TEXT("definitions/sub/item.json"), Canonical, Error);
	TestEqual(TEXT("Valid relative path is accepted"), Res4, EDocModValidationResult::Valid);
	TestTrue(TEXT("Canonical path starts with pack root"), Canonical.StartsWith(FPaths::ConvertRelativePathToFull(PackRoot)));

	// 5. The activation plan enforces the same rules (a pack with an unsafe path never gets a valid plan)
	Subsystem->RegisterProviderObject(NewObject<UDocModSampleDefinitionProvider>());
	FDocScopedTestModDir Outside(TEXT("PathTestOutside"));
	Outside.WriteFile(TEXT("escaped.json"), TEXT("{}"));
	FDocModManifest Evil;
	Evil.PackId = TEXT("evil_pack");
	Evil.PackVersion = FDocModSemVer(1, 0, 0);
	Evil.PackDirectoryPath = PackRoot;
	FDocModDefinitionEntry EvilDef;
	EvilDef.DefinitionId = TEXT("evil_pack:escape");
	EvilDef.Schema = TEXT("doc.sample.definition");
	EvilDef.RelativePath = TEXT("../PathTestOutside/escaped.json");
	Evil.Definitions.Add(EvilDef);
	FDocModActivationPlan EvilPlan;
	TestFalse(TEXT("Plan with a traversal path is invalid"), Subsystem->BuildActivationPlan({ Evil }, EvilPlan));
	TestFalse(TEXT("Plan flag is false"), EvilPlan.bIsValid);

	// 6. A hand-made 'valid' plan without validated hashes is still refused at activation
	FDocModActivationPlan Forged;
	Forged.bIsValid = true;
	Forged.OrderedPackIds = { TEXT("evil_pack") };
	const EDocModValidationResult ForgedRes = Subsystem->ActivatePlan(Forged, { Evil });
	TestNotEqual(TEXT("Forged plan is refused"), ForgedRes, EDocModValidationResult::Valid);
	TestFalse(TEXT("Nothing activated"), Subsystem->IsPackActive(TEXT("evil_pack")));

	// 7. Case-colliding paths inside one pack are rejected
	FDocScopedTestModDir CaseDir(TEXT("CaseCollisionPack"));
	CaseDir.WriteFile(TEXT("defs/item.json"), TEXT("{}"));
	FDocModManifest CasePack;
	CasePack.PackId = TEXT("case_pack");
	CasePack.PackVersion = FDocModSemVer(1, 0, 0);
	CasePack.PackDirectoryPath = CaseDir.RootDir;
	FDocModDefinitionEntry CaseA;
	CaseA.DefinitionId = TEXT("case_pack:a");
	CaseA.Schema = TEXT("doc.sample.definition");
	CaseA.RelativePath = TEXT("defs/item.json");
	FDocModDefinitionEntry CaseB = CaseA;
	CaseB.DefinitionId = TEXT("case_pack:b");
	CaseB.RelativePath = TEXT("defs/ITEM.json");
	CasePack.Definitions = { CaseA, CaseB };
	TMap<FString, FString> CaseHashes;
	TestEqual(TEXT("Case collision rejected"), Subsystem->ValidatePackContent(CasePack, CaseDir.RootDir, Error, CaseHashes), EDocModValidationResult::PathTraversal);

	IFileManager::Get().DeleteDirectory(*PackRoot, false, true);
	return true;
}

// MOD-03: Deterministic ordering handles missing versions, cycles, and conflicts
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocModDependencyOrderTest, "Doc.Mod.DependencyOrder",
	DocModTests::Flags)

bool FDocModDependencyOrderTest::RunTest(const FString& Parameters)
{
	DocModTests::FFixture Fixture;
	UDocModContentSubsystem* Subsystem = Fixture.Get();

	// Pack A (no deps), Pack B (requires A), Pack C (requires B)
	FDocModManifest PackA;
	PackA.PackId = TEXT("pack_a");
	PackA.PackVersion = FDocModSemVer(1, 0, 0);

	FDocModManifest PackB;
	PackB.PackId = TEXT("pack_b");
	PackB.PackVersion = FDocModSemVer(1, 0, 0);
	FDocModDependency DepB;
	DepB.PackId = TEXT("pack_a");
	DepB.VersionConstraint = TEXT(">=1.0.0");
	PackB.Requires.Add(DepB);

	FDocModManifest PackC;
	PackC.PackId = TEXT("pack_c");
	PackC.PackVersion = FDocModSemVer(1, 0, 0);
	FDocModDependency DepC;
	DepC.PackId = TEXT("pack_b");
	DepC.VersionConstraint = TEXT(">=1.0.0");
	PackC.Requires.Add(DepC);

	// 1. Dependency Ordering: input in reverse [C, B, A] -> output must be [A, B, C]
	TArray<FDocModManifest> Packs = { PackC, PackB, PackA };
	FDocModActivationPlan Plan;
	TestTrue(TEXT("Plan builds successfully for valid chain"), Subsystem->BuildActivationPlan(Packs, Plan));
	TestEqual(TEXT("Order count"), Plan.OrderedPackIds.Num(), 3);
	if (Plan.OrderedPackIds.Num() == 3)
	{
		TestEqual(TEXT("First is pack_a"), Plan.OrderedPackIds[0], TEXT("pack_a"));
		TestEqual(TEXT("Second is pack_b"), Plan.OrderedPackIds[1], TEXT("pack_b"));
		TestEqual(TEXT("Third is pack_c"), Plan.OrderedPackIds[2], TEXT("pack_c"));
	}

	// 2. Missing Dependency
	FDocModManifest PackMissing;
	PackMissing.PackId = TEXT("pack_missing");
	PackMissing.PackVersion = FDocModSemVer(1, 0, 0);
	FDocModDependency DepMissing;
	DepMissing.PackId = TEXT("non_existent_pack");
	PackMissing.Requires.Add(DepMissing);

	FDocModActivationPlan MissingPlan;
	TestFalse(TEXT("Missing dependency fails plan"), Subsystem->BuildActivationPlan({ PackMissing }, MissingPlan));

	// 3. Cyclic Dependency (A requires B, B requires A)
	FDocModManifest CycleA;
	CycleA.PackId = TEXT("cycle_a");
	CycleA.PackVersion = FDocModSemVer(1, 0, 0);
	FDocModDependency DepToB;
	DepToB.PackId = TEXT("cycle_b");
	CycleA.Requires.Add(DepToB);

	FDocModManifest CycleB;
	CycleB.PackId = TEXT("cycle_b");
	CycleB.PackVersion = FDocModSemVer(1, 0, 0);
	FDocModDependency DepToA;
	DepToA.PackId = TEXT("cycle_a");
	CycleB.Requires.Add(DepToA);

	FDocModActivationPlan CyclePlan;
	TestFalse(TEXT("Cycle fails plan"), Subsystem->BuildActivationPlan({ CycleA, CycleB }, CyclePlan));

	// 4. Declared Conflict
	FDocModManifest ConflictA;
	ConflictA.PackId = TEXT("conf_a");
	ConflictA.PackVersion = FDocModSemVer(1, 0, 0);
	ConflictA.Conflicts.Add(TEXT("conf_b"));

	FDocModManifest ConflictB;
	ConflictB.PackId = TEXT("conf_b");
	ConflictB.PackVersion = FDocModSemVer(1, 0, 0);

	FDocModActivationPlan ConfPlan;
	TestFalse(TEXT("Declared conflict fails plan"), Subsystem->BuildActivationPlan({ ConflictA, ConflictB }, ConfPlan));

	return true;
}

// MOD-04: Duplicate content cannot silently override another pack
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocModNamespaceCollisionTest, "Doc.Mod.NamespaceCollision",
	DocModTests::Flags)

bool FDocModNamespaceCollisionTest::RunTest(const FString& Parameters)
{
	DocModTests::FFixture Fixture;
	UDocModContentSubsystem* Subsystem = Fixture.Get();

	// Pack 1 defines "shared_item"
	FDocModManifest Pack1;
	Pack1.PackId = TEXT("pack_one");
	Pack1.PackVersion = FDocModSemVer(1, 0, 0);
	FDocModDefinitionEntry Def1;
	Def1.DefinitionId = TEXT("colliding:definition_x");
	Def1.Schema = TEXT("doc.sample.definition");
	Def1.RelativePath = TEXT("defs/item.json");
	Pack1.Definitions.Add(Def1);

	// Pack 2 defines the exact same "colliding:definition_x"
	FDocModManifest Pack2;
	Pack2.PackId = TEXT("pack_two");
	Pack2.PackVersion = FDocModSemVer(1, 0, 0);
	FDocModDefinitionEntry Def2;
	Def2.DefinitionId = TEXT("colliding:definition_x");
	Def2.Schema = TEXT("doc.sample.definition");
	Def2.RelativePath = TEXT("defs/item.json");
	Pack2.Definitions.Add(Def2);

	FDocModActivationPlan Plan;
	TestFalse(TEXT("Namespace collision across packs fails plan building"), Subsystem->BuildActivationPlan({ Pack1, Pack2 }, Plan));

	// Also verify un-namespaced definition ID fails manifest validation
	FString Error;
	EDocModValidationResult Res = Subsystem->ValidateManifest(Pack1, Error);
	TestEqual(TEXT("Definition not prefixed with pack_id: fails validation"), Res, EDocModValidationResult::NamespaceCollision);

	// A later plan cannot overwrite a definition owned by an already-active pack
	Subsystem->RegisterProviderObject(NewObject<UDocModSampleDefinitionProvider>());
	FDocScopedTestModDir DirA(TEXT("OwnerA"));
	FDocScopedTestModDir DirB(TEXT("OwnerB"));
	DirA.WriteFile(TEXT("defs/x.json"), TEXT("{\"from\": \"a\"}"));
	DirB.WriteFile(TEXT("defs/x.json"), TEXT("{\"from\": \"b\"}"));
	FDocModManifest OwnerA;
	OwnerA.PackId = TEXT("owner_a");
	OwnerA.PackVersion = FDocModSemVer(1, 0, 0);
	OwnerA.PackDirectoryPath = DirA.RootDir;
	FDocModDefinitionEntry OwnedDef;
	OwnedDef.DefinitionId = TEXT("owner_a:x");
	OwnedDef.Schema = TEXT("doc.sample.definition");
	OwnedDef.RelativePath = TEXT("defs/x.json");
	OwnerA.Definitions.Add(OwnedDef);
	FDocModActivationPlan PlanA;
	TestTrue(TEXT("Owner plan"), Subsystem->BuildActivationPlan({ OwnerA }, PlanA));
	TestEqual(TEXT("Owner activates"), Subsystem->ActivatePlan(PlanA, { OwnerA }), EDocModValidationResult::Valid);

	FDocModManifest Hijack;
	Hijack.PackId = TEXT("owner_b");
	Hijack.PackVersion = FDocModSemVer(1, 0, 0);
	Hijack.PackDirectoryPath = DirB.RootDir;
	Hijack.Definitions.Add(OwnedDef); // same definition id, different pack
	FDocModActivationPlan PlanB;
	TestFalse(TEXT("Plan overwriting an active pack's definition is refused"), Subsystem->BuildActivationPlan({ Hijack }, PlanB));
	FDocModDefinitionEntry Still;
	TestTrue(TEXT("Original definition still registered"), Subsystem->FindDefinition(TEXT("owner_a:x"), Still));

	return true;
}

// MOD-05: Provider failure leaves the prior catalog wholly intact
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocModTransactionalActivationTest, "Doc.Mod.TransactionalActivation",
	DocModTests::Flags)

bool FDocModTransactionalActivationTest::RunTest(const FString& Parameters)
{
	DocModTests::FFixture Fixture;
	UDocModContentSubsystem* Subsystem = Fixture.Get();
	UDocModSampleDefinitionProvider* Provider = NewObject<UDocModSampleDefinitionProvider>();
	Subsystem->RegisterProviderObject(Provider);

	FDocScopedTestModDir ModDir(TEXT("TransactTest"));
	ModDir.WriteFile(TEXT("defs/def1.json"), TEXT("{\"title\": \"Item 1\"}"));
	ModDir.WriteFile(TEXT("defs/def2.json"), TEXT("{\"title\": \"Item 2\"}"));

	FDocModManifest Pack;
	Pack.PackId = TEXT("transact_pack");
	Pack.PackVersion = FDocModSemVer(1, 0, 0);
	Pack.PackDirectoryPath = ModDir.RootDir;

	FDocModDefinitionEntry D1;
	D1.DefinitionId = TEXT("transact_pack:def1");
	D1.Schema = TEXT("doc.sample.definition");
	D1.RelativePath = TEXT("defs/def1.json");
	Pack.Definitions.Add(D1);

	FDocModDefinitionEntry D2;
	D2.DefinitionId = TEXT("transact_pack:def2");
	D2.Schema = TEXT("doc.sample.definition");
	D2.RelativePath = TEXT("defs/def2.json");
	Pack.Definitions.Add(D2);

	FDocModActivationPlan Plan;
	TestTrue(TEXT("Build valid plan"), Subsystem->BuildActivationPlan({ Pack }, Plan));

	int64 PriorRevision = Subsystem->GetCurrentCatalogRevision().RevisionNumber;

	// Simulate failure on def2 during staging
	Provider->bSimulateStageFailure = true;
	Provider->SimulateFailureDefinitionId = TEXT("transact_pack:def2");

	EDocModValidationResult ActRes = Subsystem->ActivatePlan(Plan, { Pack });
	TestEqual(TEXT("Staging failure returns ProviderError"), ActRes, EDocModValidationResult::ProviderError);

	// Verify rollback: no definitions committed or staged in provider, revision unchanged
	TestEqual(TEXT("Provider committed count is 0"), Provider->GetCommittedCount(), 0);
	TestEqual(TEXT("Provider staged count is 0"), Provider->GetStagedCount(), 0);
	TestEqual(TEXT("Catalog revision unchanged"), Subsystem->GetCurrentCatalogRevision().RevisionNumber, PriorRevision);
	TestFalse(TEXT("Pack not active in catalog"), Subsystem->IsPackActive(TEXT("transact_pack")));

	// Now remove simulated failure and verify clean atomic activation
	Provider->bSimulateStageFailure = false;
	EDocModValidationResult SuccessRes = Subsystem->ActivatePlan(Plan, { Pack });
	TestEqual(TEXT("Clean activation succeeds"), SuccessRes, EDocModValidationResult::Valid);
	TestEqual(TEXT("Both definitions committed"), Provider->GetCommittedCount(), 2);
	TestTrue(TEXT("Def1 committed"), Provider->HasCommittedDefinition(TEXT("transact_pack:def1")));
	TestTrue(TEXT("Def2 committed"), Provider->HasCommittedDefinition(TEXT("transact_pack:def2")));
	TestTrue(TEXT("Catalog revision incremented"), Subsystem->GetCurrentCatalogRevision().RevisionNumber > PriorRevision);
	TestTrue(TEXT("Pack is active in catalog"), Subsystem->IsPackActive(TEXT("transact_pack")));

	return true;
}

// MOD-06: Changed bytes between validation and activation invalidate the plan
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocModValidatedByteIdentityTest, "Doc.Mod.ValidatedByteIdentity",
	DocModTests::Flags)

bool FDocModValidatedByteIdentityTest::RunTest(const FString& Parameters)
{
	DocModTests::FFixture Fixture;
	UDocModContentSubsystem* Subsystem = Fixture.Get();
	UDocModSampleDefinitionProvider* Provider = NewObject<UDocModSampleDefinitionProvider>();
	Subsystem->RegisterProviderObject(Provider);

	FDocScopedTestModDir ModDir(TEXT("ByteIdentityTest"));
	ModDir.WriteFile(TEXT("defs/item.json"), TEXT("{\"original\": true}"));

	FDocModManifest Pack;
	Pack.PackId = TEXT("tamper_pack");
	Pack.PackVersion = FDocModSemVer(1, 0, 0);
	Pack.PackDirectoryPath = ModDir.RootDir;

	FDocModDefinitionEntry Def;
	Def.DefinitionId = TEXT("tamper_pack:item");
	Def.Schema = TEXT("doc.sample.definition");
	Def.RelativePath = TEXT("defs/item.json");
	Pack.Definitions.Add(Def);

	FDocModActivationPlan Plan;
	TestTrue(TEXT("Plan created and bytes validated"), Subsystem->BuildActivationPlan({ Pack }, Plan));

	// Now tamper with the file content on disk before activation
	ModDir.WriteFile(TEXT("defs/item.json"), TEXT("{\"tampered\": true}"));

	EDocModValidationResult Res = Subsystem->ActivatePlan(Plan, { Pack });
	TestEqual(TEXT("Byte mismatch rejects activation"), Res, EDocModValidationResult::ByteHashMismatch);
	TestEqual(TEXT("Nothing committed in provider"), Provider->GetCommittedCount(), 0);
	TestFalse(TEXT("Tampered pack is not active"), Subsystem->IsPackActive(TEXT("tamper_pack")));

	// A definition that was never part of the validated plan is refused, even with honest bytes
	ModDir.WriteFile(TEXT("defs/item.json"), TEXT("{\"original\": true}"));
	ModDir.WriteFile(TEXT("defs/extra.json"), TEXT("{\"extra\": true}"));
	FDocModManifest Extended = Pack;
	FDocModDefinitionEntry Extra;
	Extra.DefinitionId = TEXT("tamper_pack:extra");
	Extra.Schema = TEXT("doc.sample.definition");
	Extra.RelativePath = TEXT("defs/extra.json");
	Extended.Definitions.Add(Extra);
	TestEqual(TEXT("Unvalidated definition rejects activation"), Subsystem->ActivatePlan(Plan, { Extended }), EDocModValidationResult::ByteHashMismatch);
	TestEqual(TEXT("Still nothing committed"), Provider->GetCommittedCount(), 0);
	TestFalse(TEXT("Pack still inactive"), Subsystem->IsPackActive(TEXT("tamper_pack")));

	// Restoring the exact validated bytes makes the same plan activate
	TestEqual(TEXT("Original bytes activate"), Subsystem->ActivatePlan(Plan, { Pack }), EDocModValidationResult::Valid);
	TestEqual(TEXT("Exactly one definition committed"), Provider->GetCommittedCount(), 1);
	TestTrue(TEXT("Validated definition committed"), Provider->HasCommittedDefinition(TEXT("tamper_pack:item")));
	TestTrue(TEXT("Pack active after clean activation"), Subsystem->IsPackActive(TEXT("tamper_pack")));

	return true;
}

// MOD-07: In-use definitions prevent unsafe removal
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocModPinAndDeactivateTest, "Doc.Mod.PinAndDeactivate",
	DocModTests::Flags)

bool FDocModPinAndDeactivateTest::RunTest(const FString& Parameters)
{
	DocModTests::FFixture Fixture;
	UDocModContentSubsystem* Subsystem = Fixture.Get();
	UDocModSampleDefinitionProvider* Provider = NewObject<UDocModSampleDefinitionProvider>();
	Subsystem->RegisterProviderObject(Provider);

	FDocScopedTestModDir ModDir(TEXT("PinTest"));
	ModDir.WriteFile(TEXT("defs/item.json"), TEXT("{\"pinned\": true}"));

	FDocModManifest Pack;
	Pack.PackId = TEXT("pin_pack");
	Pack.PackVersion = FDocModSemVer(1, 0, 0);
	Pack.PackDirectoryPath = ModDir.RootDir;

	FDocModDefinitionEntry Def;
	Def.DefinitionId = TEXT("pin_pack:item");
	Def.Schema = TEXT("doc.sample.definition");
	Def.RelativePath = TEXT("defs/item.json");
	Pack.Definitions.Add(Def);

	FDocModActivationPlan Plan;
	Subsystem->BuildActivationPlan({ Pack }, Plan);
	Subsystem->ActivatePlan(Plan, { Pack });
	TestTrue(TEXT("Pack active"), Subsystem->IsPackActive(TEXT("pin_pack")));

	// Acquire pin on the active definition
	FDocModPinHandle Pin = Subsystem->AcquireDefinitionPin(TEXT("pin_pack:item"));
	TestTrue(TEXT("Pin is valid"), Pin.IsValid());
	TestTrue(TEXT("Definition is pinned"), Subsystem->IsDefinitionPinned(TEXT("pin_pack:item")));
	TestEqual(TEXT("Pin count is 1"), Subsystem->GetActivePinCount(TEXT("pin_pack:item")), 1);

	// Attempt deactivation while pinned -> must return InUse and preserve pack
	EDocModValidationResult DeactRes = Subsystem->RequestDeactivatePack(TEXT("pin_pack"));
	TestEqual(TEXT("Deactivation rejected while pinned"), DeactRes, EDocModValidationResult::InUse);
	TestTrue(TEXT("Pack remains active"), Subsystem->IsPackActive(TEXT("pin_pack")));
	TestTrue(TEXT("Provider still has item"), Provider->HasCommittedDefinition(TEXT("pin_pack:item")));

	// Release pin and retry deactivation
	TestTrue(TEXT("Release pin"), Subsystem->ReleaseDefinitionPin(Pin));
	TestFalse(TEXT("Definition no longer pinned"), Subsystem->IsDefinitionPinned(TEXT("pin_pack:item")));

	EDocModValidationResult FinalDeactRes = Subsystem->RequestDeactivatePack(TEXT("pin_pack"));
	TestEqual(TEXT("Deactivation succeeds after release"), FinalDeactRes, EDocModValidationResult::Valid);
	TestFalse(TEXT("Pack no longer active"), Subsystem->IsPackActive(TEXT("pin_pack")));
	TestFalse(TEXT("Provider item deactivated"), Provider->HasCommittedDefinition(TEXT("pin_pack:item")));

	return true;
}

// MOD-08: Missing packs preserve/quarantine records without destructive save rewrite
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocModMissingSaveContentTest, "Doc.Mod.MissingSaveContent",
	DocModTests::Flags)

bool FDocModMissingSaveContentTest::RunTest(const FString& Parameters)
{
	DocModTests::FFixture Fixture;
	UDocModContentSubsystem* Subsystem = Fixture.Get();

	// Simulate loading saved data with a reference to missing mod content
	FString MissingPack = TEXT("uninstalled_mod_v2");
	FString MissingDef = TEXT("uninstalled_mod_v2:custom_sword");
	FString PayloadJson = TEXT("{\"durability\": 100, \"enchantment\": \"fire\"}");

	TestFalse(TEXT("Missing pack is not active"), Subsystem->IsPackActive(MissingPack));

	// Quarantine missing content
	Subsystem->QuarantineMissingContent(MissingPack, MissingDef, PayloadJson, TEXT("Missing pack on save load"));

	TArray<FDocModQuarantineRecord> Quarantined = Subsystem->GetQuarantinedRecords();
	TestEqual(TEXT("Quarantine count"), Quarantined.Num(), 1);
	if (Quarantined.Num() > 0)
	{
		TestEqual(TEXT("Quarantined PackId"), Quarantined[0].PackId, MissingPack);
		TestEqual(TEXT("Quarantined DefId"), Quarantined[0].DefinitionId, MissingDef);
		TestEqual(TEXT("Payload JSON preserved"), Quarantined[0].PreservedPayloadJson, PayloadJson);
		TestTrue(TEXT("Quarantine reason recorded"), Quarantined[0].QuarantineReason.Contains(TEXT("Missing pack")));
	}

	return true;
}

// MOD-09: Base packs cannot load arbitrary code/classes/assets or execute commands
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocModNoExecutableContentTest, "Doc.Mod.NoExecutableContent",
	DocModTests::Flags)

bool FDocModNoExecutableContentTest::RunTest(const FString& Parameters)
{
	DocModTests::FFixture Fixture;
	UDocModContentSubsystem* Subsystem = Fixture.Get();
	FString PackRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestMods"), TEXT("ExecTestPack"));
	IFileManager::Get().MakeDirectory(*PackRoot, true);

	FString Canonical;
	FString Error;

	// 1. DLL file attempt
	EDocModValidationResult DllRes = Subsystem->ValidatePathSafety(PackRoot, TEXT("binaries/payload.dll"), Canonical, Error);
	TestEqual(TEXT(".dll is blocked"), DllRes, EDocModValidationResult::ExecutableContentBlocked);

	// 2. Executable EXE attempt
	EDocModValidationResult ExeRes = Subsystem->ValidatePathSafety(PackRoot, TEXT("scripts/setup.exe"), Canonical, Error);
	TestEqual(TEXT(".exe is blocked"), ExeRes, EDocModValidationResult::ExecutableContentBlocked);

	// 3. Unreal binary asset (.uasset) attempt
	EDocModValidationResult UassetRes = Subsystem->ValidatePathSafety(PackRoot, TEXT("content/weapon.uasset"), Canonical, Error);
	TestEqual(TEXT(".uasset is blocked"), UassetRes, EDocModValidationResult::ExecutableContentBlocked);

	// 4. Batch script (.bat) attempt
	EDocModValidationResult BatRes = Subsystem->ValidatePathSafety(PackRoot, TEXT("launch.bat"), Canonical, Error);
	TestEqual(TEXT(".bat is blocked"), BatRes, EDocModValidationResult::ExecutableContentBlocked);

	// 5. Extensions outside the data allowlist are refused too
	TestEqual(TEXT(".py is not data"), Subsystem->ValidatePathSafety(PackRoot, TEXT("tools/run.py"), Canonical, Error), EDocModValidationResult::ExecutableContentBlocked);
	TestEqual(TEXT(".json is allowed"), Subsystem->ValidatePathSafety(PackRoot, TEXT("defs/item.json"), Canonical, Error), EDocModValidationResult::Valid);

	// 6. A pack listing a blocked file never produces a valid plan
	Subsystem->RegisterProviderObject(NewObject<UDocModSampleDefinitionProvider>());
	FDocScopedTestModDir ExecDir(TEXT("ExecPlanPack"));
	ExecDir.WriteFile(TEXT("bin/payload.dll"), TEXT("MZ"));
	FDocModManifest ExecPack;
	ExecPack.PackId = TEXT("exec_pack");
	ExecPack.PackVersion = FDocModSemVer(1, 0, 0);
	ExecPack.PackDirectoryPath = ExecDir.RootDir;
	FDocModDefinitionEntry ExecDef;
	ExecDef.DefinitionId = TEXT("exec_pack:payload");
	ExecDef.Schema = TEXT("doc.sample.definition");
	ExecDef.RelativePath = TEXT("bin/payload.dll");
	ExecPack.Definitions.Add(ExecDef);
	FDocModActivationPlan ExecPlan;
	TestFalse(TEXT("Plan with an executable file is invalid"), Subsystem->BuildActivationPlan({ ExecPack }, ExecPlan));

	IFileManager::Get().DeleteDirectory(*PackRoot, false, true);
	return true;
}

// MOD-10: Sample provider proves loader behavior with all gameplay siblings absent
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocModIsolatedProviderTest, "Doc.Mod.IsolatedProvider",
	DocModTests::Flags)

bool FDocModIsolatedProviderTest::RunTest(const FString& Parameters)
{
	DocModTests::FFixture Fixture;
	UDocModContentSubsystem* Subsystem = Fixture.Get();
	UDocModSampleDefinitionProvider* Provider = NewObject<UDocModSampleDefinitionProvider>();
	Subsystem->RegisterProviderObject(Provider);

	// Verify sample provider schema
	TestEqual(TEXT("Provider schema"), Provider->GetSupportedSchema(), TEXT("doc.sample.definition"));
	TestEqual(TEXT("Provider schema version"), Provider->GetSupportedSchemaVersion(), 1);

	FDocScopedTestModDir ModDir(TEXT("IsolatedTest"));
	ModDir.WriteFile(TEXT("manifest.json"), TEXT(R"({
		"manifest_version": 1,
		"pack_id": "standalone_sample_pack",
		"pack_version": "1.0.0",
		"display_name": "Standalone Sample Pack",
		"requires": [],
		"conflicts": [],
		"definitions": [
			{
				"id": "standalone_sample_pack:sample_entry",
				"schema": "doc.sample.definition",
				"schema_version": 1,
				"path": "defs/sample_entry.json"
			}
		]
	})"));
	ModDir.WriteFile(TEXT("defs/sample_entry.json"), TEXT("{\"key\": \"standalone_value\"}"));

	// Discover pack
	FDocModManifest DiscoveredManifest;
	FString Error;
	TestTrue(TEXT("Discover pack"), Subsystem->DiscoverPackInDirectory(ModDir.RootDir, DiscoveredManifest, Error));
	TestEqual(TEXT("Discovered pack ID"), DiscoveredManifest.PackId, TEXT("standalone_sample_pack"));

	// Build plan and activate
	FDocModActivationPlan Plan;
	TestTrue(TEXT("Build plan"), Subsystem->BuildActivationPlan({ DiscoveredManifest }, Plan));
	TestEqual(TEXT("Plan pack order count"), Plan.OrderedPackIds.Num(), 1);

	EDocModValidationResult ActRes = Subsystem->ActivatePlan(Plan, { DiscoveredManifest });
	TestEqual(TEXT("Plan activated successfully"), ActRes, EDocModValidationResult::Valid);

	// Query definition
	FDocModDefinitionEntry FoundDef;
	TestTrue(TEXT("Definition found in catalog"), Subsystem->FindDefinition(TEXT("standalone_sample_pack:sample_entry"), FoundDef));
	TestEqual(TEXT("Definition ID matches"), FoundDef.DefinitionId, TEXT("standalone_sample_pack:sample_entry"));
	TestTrue(TEXT("Provider committed definition"), Provider->HasCommittedDefinition(TEXT("standalone_sample_pack:sample_entry")));
	TestTrue(TEXT("Content preserved"), Provider->GetCommittedDefinitionContent(TEXT("standalone_sample_pack:sample_entry")).Contains(TEXT("standalone_value")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
