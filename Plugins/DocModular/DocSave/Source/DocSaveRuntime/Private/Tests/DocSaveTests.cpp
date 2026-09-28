// DocSave automation tests (handoff Section 13 / SAV-*). Worlds are created with
// FDocScopedTestWorld; the save subsystem is created directly and injected via
// SetSubsystemOverrideForTesting, with an in-memory backend (plus one test that
// exercises the local file backend in the automation transient directory).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocSaveGameSubsystem.h"
#include "DocSaveStorage.h"
#include "DocSaveableComponent.h"
#include "DocSharedTypes.h"
#include "Tests/DocSaveTestTypes.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace DocSaveTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	/** Thread-safe in-memory backend with the same "keep previous as backup" behaviour as the file backend. */
	class FMemoryBackend final : public IDocSaveStorageBackend
	{
	public:
		virtual bool WriteSlot(const FString& Slot, const TArray<uint8>& Bytes, FString& OutError) override
		{
			FScopeLock Lock(&Mutex);
			if (bFailWrites) { OutError = TEXT("Simulated write failure"); return false; }
			if (const TArray<uint8>* Current = Files.Find(Slot)) { Backups.Add(Slot, *Current); }
			Files.Add(Slot, Bytes);
			return true;
		}
		virtual bool ReadSlot(const FString& Slot, TArray<uint8>& OutBytes, int64 MaxBytes, FString& OutError) override
		{
			return Read(Files, Slot, OutBytes, MaxBytes, OutError);
		}
		virtual bool SlotExists(const FString& Slot) const override { FScopeLock Lock(&Mutex); return Files.Contains(Slot); }
		virtual bool ReadBackup(const FString& Slot, TArray<uint8>& OutBytes, int64 MaxBytes, FString& OutError) override
		{
			return Read(Backups, Slot, OutBytes, MaxBytes, OutError);
		}
		virtual bool DeleteSlot(const FString& Slot, FString& OutError) override
		{
			FScopeLock Lock(&Mutex);
			Files.Remove(Slot);
			Backups.Remove(Slot);
			return true;
		}
		virtual bool SupportsAtomicReplace() const override { return true; }

		void Poke(const FString& Slot, const TArray<uint8>& Bytes, bool bBackup = false)
		{
			FScopeLock Lock(&Mutex);
			(bBackup ? Backups : Files).Add(Slot, Bytes);
		}
		TArray<uint8> Peek(const FString& Slot) const { FScopeLock Lock(&Mutex); const TArray<uint8>* F = Files.Find(Slot); return F ? *F : TArray<uint8>(); }

		bool bFailWrites = false;

	private:
		bool Read(const TMap<FString, TArray<uint8>>& Map, const FString& Slot, TArray<uint8>& OutBytes, int64 MaxBytes, FString& OutError) const
		{
			FScopeLock Lock(&Mutex);
			const TArray<uint8>* F = Map.Find(Slot);
			if (!F) { OutError = TEXT("Slot not found"); return false; }
			if (F->Num() > MaxBytes) { OutError = TEXT("Too large"); return false; }
			OutBytes = *F;
			return true;
		}
		mutable FCriticalSection Mutex;
		TMap<FString, TArray<uint8>> Files;
		TMap<FString, TArray<uint8>> Backups;
	};

	/** World + injected save subsystem. Destroy order: override cleared, then world. */
	struct FFixture
	{
		TSharedPtr<FMemoryBackend> Backend;
		FDocScopedTestWorld TW;
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UDocSaveGameSubsystem> Save;

		explicit FFixture(TSharedPtr<FMemoryBackend> InBackend = nullptr)
			: Backend(InBackend.IsValid() ? InBackend : MakeShared<FMemoryBackend>())
		{
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			Save.Reset(NewObject<UDocSaveGameSubsystem>(GameInstance.Get()));
			Save->InitializeForTesting(Backend);
			Save->AllowPayloadStruct(FDocSaveTestState::StaticStruct());
			UDocSaveGameSubsystem::SetSubsystemOverrideForTesting(Save.Get());
		}

		~FFixture()
		{
			UDocSaveGameSubsystem::SetSubsystemOverrideForTesting(nullptr);
		}

		UWorld* World() const { return TW.World; }

		/** Spawn a test actor. bAuthored simulates a level-placed actor. */
		ADocSaveTestActor* Spawn(bool bAuthored, const FVector& Location = FVector::ZeroVector, const FDocPersistentObjectId* Id = nullptr, int32 Counter = 0)
		{
			const FTransform T(Location);
			ADocSaveTestActor* A = World()->SpawnActorDeferred<ADocSaveTestActor>(ADocSaveTestActor::StaticClass(), T, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!A) { return nullptr; }
			A->Saveable->MarkRuntimeSpawned(!bAuthored);
			if (Id) { A->Saveable->AssignPersistentId(*Id); }
			A->State->Counter = Counter;
			A->FinishSpawning(T);
			if (!A->HasActorBegunPlay())
			{
				A->DispatchBeginPlay();
			}
			return A;
		}

		ADocSaveTestActor* FindById(const FDocPersistentObjectId& Id) const
		{
			for (TActorIterator<ADocSaveTestActor> It(World()); It; ++It)
			{
				if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->Saveable->GetDocPersistentId_Implementation() == Id)
				{
					return *It;
				}
			}
			return nullptr;
		}
	};

	TArray<uint8> MakeBody(int32 Size)
	{
		TArray<uint8> Body;
		Body.SetNumUninitialized(Size);
		for (int32 i = 0; i < Size; ++i) { Body[i] = uint8((i * 31) ^ (i >> 3)); }
		return Body;
	}
}

using namespace DocSaveTests;

// ---------------------------------------------------------------------------
// Envelope and storage (pure)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSaveEnvelopeRoundTripTest, "Doc.Save.Envelope.RoundTrip", Flags)
bool FDocSaveEnvelopeRoundTripTest::RunTest(const FString& Parameters)
{
	const TArray<uint8> Body = MakeBody(4096);
	for (const bool bCompress : { true, false })
	{
		FDocSaveHeader Header;
		Header.SaveRevision = 42;
		Header.ProducerGameVersion = TEXT("9.9");
		Header.Category = EDocSaveSlotCategory::Checkpoint;
		TArray<uint8> File;
		FString Error;
		TestTrue(TEXT("Write"), FDocSaveEnvelope::Write(Header, Body, bCompress, File, Error));

		FDocSaveHeader Out;
		TArray<uint8> OutBody;
		TestTrue(FString::Printf(TEXT("Read (compress=%d): %s"), bCompress, *Error), FDocSaveEnvelope::Read(File, FDocSaveEnvelope::FLimits(), Out, OutBody, Error));
		TestEqual(TEXT("Body preserved"), OutBody, Body);
		TestEqual(TEXT("Revision"), Out.SaveRevision, int64(42));
		TestEqual(TEXT("Version string"), Out.ProducerGameVersion, FString(TEXT("9.9")));
		TestEqual(TEXT("Category"), Out.Category, EDocSaveSlotCategory::Checkpoint);
		TestEqual(TEXT("Compression mode"), Out.CompressionMode, bCompress ? 1 : 0);

		FDocSaveHeader HeaderOnly;
		TestTrue(TEXT("ReadHeader"), FDocSaveEnvelope::ReadHeader(File, HeaderOnly, Error));
		TestEqual(TEXT("Header-only revision"), HeaderOnly.SaveRevision, int64(42));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSaveEnvelopeRejectsTest, "Doc.Save.Envelope.RejectsBadFiles", Flags)
bool FDocSaveEnvelopeRejectsTest::RunTest(const FString& Parameters)
{
	const TArray<uint8> Body = MakeBody(2048);
	TArray<uint8> File;
	FString Error;
	FDocSaveEnvelope::Write(FDocSaveHeader(), Body, /*bCompress*/ false, File, Error);
	FDocSaveHeader H;
	TArray<uint8> Out;

	{
		TArray<uint8> Corrupt = File;
		Corrupt[Corrupt.Num() - 10] ^= 0xFF; // payload byte
		TestFalse(TEXT("Flipped payload byte rejected"), FDocSaveEnvelope::Read(Corrupt, FDocSaveEnvelope::FLimits(), H, Out, Error));
	}
	{
		TArray<uint8> Truncated = File;
		Truncated.SetNum(Truncated.Num() - 100);
		TestFalse(TEXT("Truncated file rejected"), FDocSaveEnvelope::Read(Truncated, FDocSaveEnvelope::FLimits(), H, Out, Error));
		TArray<uint8> Tiny = { 1, 2, 3 };
		TestFalse(TEXT("Tiny file rejected"), FDocSaveEnvelope::Read(Tiny, FDocSaveEnvelope::FLimits(), H, Out, Error));
		TestFalse(TEXT("Empty file rejected"), FDocSaveEnvelope::Read(TArray<uint8>(), FDocSaveEnvelope::FLimits(), H, Out, Error));
	}
	{
		TArray<uint8> BadMagic = File;
		BadMagic[0] ^= 0xFF;
		TestFalse(TEXT("Bad magic rejected"), FDocSaveEnvelope::Read(BadMagic, FDocSaveEnvelope::FLimits(), H, Out, Error));
	}
	{
		TArray<uint8> Future = File;
		const int32 FutureVersion = FDocSaveHeader::CurrentFormatVersion + 1;
		FMemory::Memcpy(Future.GetData() + 4, &FutureVersion, sizeof(int32)); // layout: uint32 magic, int32 format
		TestFalse(TEXT("Future format rejected"), FDocSaveEnvelope::Read(Future, FDocSaveEnvelope::FLimits(), H, Out, Error));
		TestTrue(TEXT("Future format reported as unsupported"), Error.StartsWith(TEXT("Unsupported format")));
	}
	{
		FDocSaveEnvelope::FLimits Small;
		Small.MaxFileBytes = 64;
		TestFalse(TEXT("File over MaxFileBytes rejected"), FDocSaveEnvelope::Read(File, Small, H, Out, Error));
		FDocSaveEnvelope::FLimits SmallUncompressed;
		SmallUncompressed.MaxUncompressedBytes = 16;
		TestFalse(TEXT("Declared size over limit rejected"), FDocSaveEnvelope::Read(File, SmallUncompressed, H, Out, Error));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSaveSlotNamesTest, "Doc.Save.SlotNames", Flags)
bool FDocSaveSlotNamesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Simple"), FDocLocalFileSaveBackend::IsValidSlotName(TEXT("Slot_01-a")));
	TestFalse(TEXT("Empty"), FDocLocalFileSaveBackend::IsValidSlotName(TEXT("")));
	TestFalse(TEXT("Traversal"), FDocLocalFileSaveBackend::IsValidSlotName(TEXT("../evil")));
	TestFalse(TEXT("Separator"), FDocLocalFileSaveBackend::IsValidSlotName(TEXT("a/b")));
	TestFalse(TEXT("Backslash"), FDocLocalFileSaveBackend::IsValidSlotName(TEXT("a\\b")));
	TestFalse(TEXT("Drive"), FDocLocalFileSaveBackend::IsValidSlotName(TEXT("C:x")));
	TestFalse(TEXT("Dot"), FDocLocalFileSaveBackend::IsValidSlotName(TEXT("a.docsav")));
	TestFalse(TEXT("Too long"), FDocLocalFileSaveBackend::IsValidSlotName(FString::ChrN(65, TEXT('a'))));
	TestTrue(TEXT("Max length"), FDocLocalFileSaveBackend::IsValidSlotName(FString::ChrN(64, TEXT('a'))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSaveLocalBackendFaultTest, "Doc.Save.LocalBackend.FaultInjection", Flags)
bool FDocSaveLocalBackendFaultTest::RunTest(const FString& Parameters)
{
	const FString Dir = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("DocSaveTest"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FDocLocalFileSaveBackend Backend(Dir);
	const TArray<uint8> A = MakeBody(300);
	TArray<uint8> B = MakeBody(500);
	B[0] = 0xAB;
	FString Error;
	TArray<uint8> Read;

	TestTrue(TEXT("First write"), Backend.WriteSlot(TEXT("Fault"), A, Error));
	TestTrue(TEXT("Exists"), Backend.SlotExists(TEXT("Fault")));

	Backend.bFailBeforeCommitForTesting = true;
	TestFalse(TEXT("Write that fails before commit reports failure"), Backend.WriteSlot(TEXT("Fault"), B, Error));
	TestTrue(TEXT("Previous save still readable"), Backend.ReadSlot(TEXT("Fault"), Read, 1 << 20, Error));
	TestEqual(TEXT("Previous save intact"), Read, A);

	Backend.bFailBeforeCommitForTesting = false;
	TestTrue(TEXT("Second write"), Backend.WriteSlot(TEXT("Fault"), B, Error));
	TestTrue(TEXT("Read new"), Backend.ReadSlot(TEXT("Fault"), Read, 1 << 20, Error));
	TestEqual(TEXT("New save committed"), Read, B);
	TestTrue(TEXT("Backup readable"), Backend.ReadBackup(TEXT("Fault"), Read, 1 << 20, Error));
	TestEqual(TEXT("Backup is last-known-good"), Read, A);

	TestFalse(TEXT("Bounded read refuses oversize"), Backend.ReadSlot(TEXT("Fault"), Read, 10, Error));
	TestFalse(TEXT("Invalid slot refused"), Backend.WriteSlot(TEXT("../x"), A, Error));

	TestTrue(TEXT("Delete"), Backend.DeleteSlot(TEXT("Fault"), Error));
	TestFalse(TEXT("Gone"), Backend.SlotExists(TEXT("Fault")));
	IFileManager::Get().DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

// ---------------------------------------------------------------------------
// Subsystem
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSaveRoundTripTest, "Doc.Save.RoundTrip", Flags)
bool FDocSaveRoundTripTest::RunTest(const FString& Parameters)
{
	FFixture F;
	ADocSaveTestActor* A = F.Spawn(/*bAuthored*/ true, FVector(100, 0, 0), nullptr, 7);
	if (!TestNotNull(TEXT("Actor"), A)) { return false; }
	A->State->Label = TEXT("Door");
	const FDocPersistentObjectId Id = A->Saveable->GetDocPersistentId_Implementation();
	TestTrue(TEXT("Has id"), Id.IsValid());
	TestFalse(TEXT("Authored"), A->Saveable->IsRuntimeSpawned());

	int64 Revision = 0;
	const FDocSystemResult Saved = F.Save->SaveToSlotBlocking(F.World(), TEXT("RoundTrip"), EDocSaveSlotCategory::Manual, Revision);
	TestTrue(FString::Printf(TEXT("Saved: %s"), *Saved.ToString()), Saved.IsSuccess());
	TestEqual(TEXT("First revision"), Revision, int64(1));

	A->State->Counter = 1;
	A->State->Label = TEXT("Changed");
	A->SetActorLocation(FVector(0, 500, 0));

	int64 Loaded = 0;
	const FDocSystemResult Load = F.Save->LoadFromSlotBlocking(F.World(), TEXT("RoundTrip"), Loaded);
	TestTrue(FString::Printf(TEXT("Loaded: %s"), *Load.ToString()), Load.IsSuccess());
	TestEqual(TEXT("Loaded revision"), Loaded, int64(1));
	TestEqual(TEXT("Counter restored"), A->State->Counter, 7);
	TestEqual(TEXT("Label restored"), A->State->Label, FString(TEXT("Door")));
	TestTrue(TEXT("Transform restored"), A->GetActorLocation().Equals(FVector(100, 0, 0), 0.01));
	TestTrue(TEXT("References resolved (phase 2)"), A->State->ResolveCalls > 0);
	TestFalse(TEXT("Barrier released"), F.Save->IsRestoring());

	int64 Missing = 0;
	TestEqual(TEXT("Missing slot"), F.Save->LoadFromSlotBlocking(F.World(), TEXT("Nope"), Missing).Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("Invalid slot"), F.Save->SaveToSlotBlocking(F.World(), TEXT("../bad"), EDocSaveSlotCategory::Manual, Missing).Outcome, EDocResultOutcome::InvalidInput);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSaveStreamedOutTest, "Doc.Save.StreamedOutRetention", Flags)
bool FDocSaveStreamedOutTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FMemoryBackend> Backend = MakeShared<FMemoryBackend>();
	FDocPersistentObjectId Id;
	{
		FFixture F(Backend);
		ADocSaveTestActor* A = F.Spawn(true, FVector(10, 20, 30), nullptr, 5);
		if (!TestNotNull(TEXT("Actor"), A)) { return false; }
		Id = A->Saveable->GetDocPersistentId_Implementation();

		A->Destroy(); // unload (EndPlay), not intentional destruction
		const FDocSaveObjectRecord* Record = F.Save->GetRecords().Find(Id);
		if (!TestNotNull(TEXT("Record retained after unload"), Record)) { return false; }
		TestEqual(TEXT("Lifecycle StreamedOut"), Record->Lifecycle, EDocSaveLifecycle::StreamedOut);
		TestFalse(TEXT("Not tombstoned"), F.Save->IsTombstoned(Id));

		// Stream back in: same identity → retained state applied on registration.
		ADocSaveTestActor* Again = F.Spawn(true, FVector::ZeroVector, &Id, 0);
		if (!TestNotNull(TEXT("Streamed back"), Again)) { return false; }
		TestEqual(TEXT("State re-applied on stream-in"), Again->State->Counter, 5);
		TestTrue(TEXT("Transform re-applied"), Again->GetActorLocation().Equals(FVector(10, 20, 30), 0.01));

		Again->State->Counter = 6;
		Again->Destroy();
		int64 Revision = 0;
		TestTrue(TEXT("Save with unloaded object"), F.Save->SaveToSlotBlocking(F.World(), TEXT("Streamed"), EDocSaveSlotCategory::Auto, Revision).IsSuccess());
	}
	{
		// A later session: the unloaded object's latest state is in the file.
		FFixture F(Backend);
		int64 Revision = 0;
		F.Save->LoadFromSlotBlocking(F.World(), TEXT("Streamed"), Revision); // different world namespace: nothing to apply here
		const FDocSaveObjectRecord* Record = F.Save->GetRecords().Find(Id);
		if (!TestNotNull(TEXT("Record loaded"), Record)) { return false; }
		TestEqual(TEXT("Still StreamedOut"), Record->Lifecycle, EDocSaveLifecycle::StreamedOut);
		TestEqual(TEXT("One component payload"), Record->Components.Num(), 1);
		FDocSavePayload Payload;
		if (Record->Components.Num() == 1 && TestTrue(TEXT("Decode"), F.Save->DecodePayload(Record->Components[0], Payload).IsSuccess()))
		{
			const FDocSaveTestState* State = Payload.Data.GetPtr<FDocSaveTestState>();
			TestTrue(TEXT("Latest unloaded state saved"), State && State->Counter == 6);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSaveTombstoneTest, "Doc.Save.Tombstone", Flags)
bool FDocSaveTombstoneTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FMemoryBackend> Backend = MakeShared<FMemoryBackend>();
	FDocPersistentObjectId Id;
	{
		FFixture F(Backend);
		ADocSaveTestActor* A = F.Spawn(true);
		if (!TestNotNull(TEXT("Actor"), A)) { return false; }
		Id = A->Saveable->GetDocPersistentId_Implementation();

		const FDocSystemResult Destroyed = A->Saveable->DestroyPersistently();
		TestTrue(TEXT("Destroyed persistently"), Destroyed.IsSuccess());
		TestTrue(TEXT("Tombstoned"), F.Save->IsTombstoned(Id));
		TestEqual(TEXT("Second mark is NoChange"), F.Save->MarkPersistentlyDestroyed(Id).Outcome, EDocResultOutcome::NoChange);
		TestEqual(TEXT("Cannot virtualize a tombstone"), F.Save->MarkVirtualized(Id, true).Outcome, EDocResultOutcome::Conflict);

		// The level streams in again: the authored actor must not come back.
		ADocSaveTestActor* Returning = F.Spawn(true, FVector::ZeroVector, &Id);
		TestTrue(TEXT("Returning actor suppressed"), !IsValid(Returning) || Returning->IsActorBeingDestroyed());
		TestNull(TEXT("No live actor with the id"), F.FindById(Id));

		int64 Revision = 0;
		TestTrue(TEXT("Save"), F.Save->SaveToSlotBlocking(F.World(), TEXT("Tomb"), EDocSaveSlotCategory::Manual, Revision).IsSuccess());
	}
	{
		FFixture F(Backend);
		int64 Revision = 0;
		F.Save->LoadFromSlotBlocking(F.World(), TEXT("Tomb"), Revision);
		TestTrue(TEXT("Tombstone survives save/load"), F.Save->IsTombstoned(Id));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSaveRuntimeSpawnTest, "Doc.Save.RuntimeSpawnRestore", Flags)
bool FDocSaveRuntimeSpawnTest::RunTest(const FString& Parameters)
{
	FFixture F;
	ADocSaveTestActor* A = F.Spawn(/*bAuthored*/ false, FVector(5, 6, 7), nullptr, 3);
	if (!TestNotNull(TEXT("Actor"), A)) { return false; }
	TestTrue(TEXT("Runtime spawned"), A->Saveable->IsRuntimeSpawned());
	const FDocPersistentObjectId Id = A->Saveable->GetDocPersistentId_Implementation();

	int64 Revision = 0;
	TestTrue(TEXT("Save"), F.Save->SaveToSlotBlocking(F.World(), TEXT("Spawned"), EDocSaveSlotCategory::Manual, Revision).IsSuccess());
	A->Destroy();
	TestNull(TEXT("Gone before load"), F.FindById(Id));

	// Not allowlisted: nothing spawned, visible failure.
	int64 Loaded = 0;
	const FDocSystemResult Refused = F.Save->LoadFromSlotBlocking(F.World(), TEXT("Spawned"), Loaded);
	TestFalse(TEXT("Disallowed class reported"), Refused.IsSuccess());
	TestNull(TEXT("Disallowed class not spawned"), F.FindById(Id));

	F.Save->AllowRuntimeSpawnClass(ADocSaveTestActor::StaticClass());
	const FDocSystemResult Load = F.Save->LoadFromSlotBlocking(F.World(), TEXT("Spawned"), Loaded);
	TestTrue(FString::Printf(TEXT("Load: %s"), *Load.ToString()), Load.IsSuccess());
	ADocSaveTestActor* Restored = F.FindById(Id);
	if (!TestNotNull(TEXT("Recreated with saved identity"), Restored)) { return false; }
	TestEqual(TEXT("State restored"), Restored->State->Counter, 3);
	TestTrue(TEXT("Still runtime-spawned"), Restored->Saveable->IsRuntimeSpawned());
	TestTrue(TEXT("Transform restored"), Restored->GetActorLocation().Equals(FVector(5, 6, 7), 0.01));

	// Loading again must not duplicate it (it is live now).
	F.Save->LoadFromSlotBlocking(F.World(), TEXT("Spawned"), Loaded);
	int32 Count = 0;
	for (TActorIterator<ADocSaveTestActor> It(F.World()); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && It->Saveable->GetDocPersistentId_Implementation() == Id) { ++Count; }
	}
	TestEqual(TEXT("No duplicate on reload"), Count, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSaveMigrationTest, "Doc.Save.Migration", Flags)
bool FDocSaveMigrationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FMemoryBackend> Backend = MakeShared<FMemoryBackend>();
	const FString TypeId = FDocSaveTestState::StaticStruct()->GetPathName();
	FFixture F(Backend);
	ADocSaveTestActor* A = F.Spawn(true, FVector::ZeroVector, nullptr, 7);
	if (!TestNotNull(TEXT("Actor"), A)) { return false; }
	int64 Revision = 0;
	F.Save->SaveToSlotBlocking(F.World(), TEXT("Migrate"), EDocSaveSlotCategory::Manual, Revision); // payload version 1

	// Upgrade path v1 → v2 adds 100 (stands in for a real schema change).
	F.Save->RegisterCurrentVersion(TypeId, 2);
	F.Save->RegisterMigration(TypeId, 1, [](int32 From, TArray<uint8>& Bytes)
	{
		FDocSaveTestState State;
		if (!DocCoreSerialization::Decode(State, Bytes)) { return false; }
		State.Counter += 100;
		DocCoreSerialization::Encode(State, Bytes);
		return true;
	});
	A->State->Counter = 0;
	int64 Loaded = 0;
	const FDocSystemResult Load = F.Save->LoadFromSlotBlocking(F.World(), TEXT("Migrate"), Loaded);
	TestTrue(FString::Printf(TEXT("Migrated load: %s"), *Load.ToString()), Load.IsSuccess());
	TestEqual(TEXT("Migration applied"), A->State->Counter, 107);

	// A payload newer than this build understands is rejected, never guessed at.
	F.Save->RegisterCurrentVersion(TypeId, 0);
	A->State->Counter = -1;
	const FDocSystemResult Future = F.Save->LoadFromSlotBlocking(F.World(), TEXT("Migrate"), Loaded);
	TestFalse(TEXT("Future payload reported"), Future.IsSuccess());
	TestEqual(TEXT("Future payload not applied"), A->State->Counter, -1);

	// Missing migration step: visible failure.
	F.Save->RegisterCurrentVersion(TypeId, 3);
	const FDocSystemResult NoStep = F.Save->LoadFromSlotBlocking(F.World(), TEXT("Migrate"), Loaded);
	TestFalse(TEXT("Missing migration reported"), NoStep.IsSuccess());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSaveRevisionTest, "Doc.Save.Revisions", Flags)
bool FDocSaveRevisionTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FMemoryBackend> Backend = MakeShared<FMemoryBackend>();
	{
		FFixture F(Backend);
		F.Spawn(true);
		int64 R1 = 0, R2 = 0;
		F.Save->SaveToSlotBlocking(F.World(), TEXT("Rev"), EDocSaveSlotCategory::Manual, R1);
		F.Save->SaveToSlotBlocking(F.World(), TEXT("Rev"), EDocSaveSlotCategory::Manual, R2);
		TestEqual(TEXT("R1"), R1, int64(1));
		TestEqual(TEXT("R2 increases"), R2, int64(2));
		TestEqual(TEXT("Committed"), F.Save->GetLastCommittedRevision(TEXT("Rev")), int64(2));
		Backend->bFailWrites = true;
		int64 R3 = 0;
		TestFalse(TEXT("Failed write reported"), F.Save->SaveToSlotBlocking(F.World(), TEXT("Rev"), EDocSaveSlotCategory::Manual, R3).IsSuccess());
		TestEqual(TEXT("Failed write commits nothing"), F.Save->GetLastCommittedRevision(TEXT("Rev")), int64(2));
		Backend->bFailWrites = false;
	}
	{
		// New session with no in-memory slot state: continues above the committed revision.
		FFixture F(Backend);
		int64 R = 0;
		TestTrue(TEXT("Save in new session"), F.Save->SaveToSlotBlocking(F.World(), TEXT("Rev"), EDocSaveSlotCategory::Manual, R).IsSuccess());
		TestEqual(TEXT("Revision continues"), R, int64(3));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSaveBadFileTest, "Doc.Save.BadFileKeepsProgress", Flags)
bool FDocSaveBadFileTest::RunTest(const FString& Parameters)
{
	FFixture F;
	ADocSaveTestActor* A = F.Spawn(true, FVector::ZeroVector, nullptr, 9);
	if (!TestNotNull(TEXT("Actor"), A)) { return false; }
	int64 Revision = 0;
	F.Save->SaveToSlotBlocking(F.World(), TEXT("Bad"), EDocSaveSlotCategory::Manual, Revision);
	const TArray<uint8> Good = F.Backend->Peek(TEXT("Bad"));
	const int32 RecordsBefore = F.Save->GetRecords().Num();

	// Corrupt current file and no usable backup: load fails, nothing is cleared.
	TArray<uint8> Garbage = MakeBody(200);
	F.Backend->Poke(TEXT("Bad"), Garbage);
	F.Backend->Poke(TEXT("Bad"), Garbage, /*bBackup*/ true);
	A->State->Counter = 11;
	int64 Loaded = 0;
	const FDocSystemResult Failed = F.Save->LoadFromSlotBlocking(F.World(), TEXT("Bad"), Loaded);
	TestFalse(TEXT("Corrupt load fails"), Failed.IsSuccess());
	TestEqual(TEXT("Live state untouched"), A->State->Counter, 11);
	TestEqual(TEXT("Records untouched"), F.Save->GetRecords().Num(), RecordsBefore);

	// Corrupt current file but a good backup: recovered, and the result says so.
	F.Backend->Poke(TEXT("Bad"), Good, /*bBackup*/ true);
	const FDocSystemResult Recovered = F.Save->LoadFromSlotBlocking(F.World(), TEXT("Bad"), Loaded);
	TestTrue(FString::Printf(TEXT("Recovered: %s"), *Recovered.ToString()), Recovered.IsSuccess());
	TestTrue(TEXT("Diagnostic mentions backup"), Recovered.Diagnostic.Contains(TEXT("backup")));
	TestEqual(TEXT("Backup state applied"), A->State->Counter, 9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSavePayloadAllowlistTest, "Doc.Save.PayloadAllowlist", Flags)
bool FDocSavePayloadAllowlistTest::RunTest(const FString& Parameters)
{
	FFixture F;
	FDocSavePayload Payload;
	Payload.ComponentKey = TEXT("X");
	Payload.Data.InitializeAs<FDocSaveTestForbiddenState>();
	FDocSaveComponentRecord Record;
	TestEqual(TEXT("Disallowed struct refused"), F.Save->EncodePayload(Payload, Record).Outcome, EDocResultOutcome::PermissionDenied);

	FDocSavePayload NoKey;
	NoKey.Data.InitializeAs<FDocSaveTestState>();
	TestEqual(TEXT("Missing key refused"), F.Save->EncodePayload(NoKey, Record).Outcome, EDocResultOutcome::InvalidInput);

	FDocSaveComponentRecord Forged;
	Forged.ComponentKey = TEXT("X");
	Forged.TypeId = FDocSaveTestForbiddenState::StaticStruct()->GetPathName();
	FDocSavePayload Out;
	TestEqual(TEXT("Disallowed type in file refused on decode"), F.Save->DecodePayload(Forged, Out).Outcome, EDocResultOutcome::PermissionDenied);

	FDocFeatureRecord Feature;
	Feature.FeatureId = TEXT("Test.Feature");
	F.Save->SetFeatureRecord(Feature);
	FDocFeatureRecord Found;
	TestTrue(TEXT("Feature record stored"), F.Save->GetFeatureRecord(TEXT("Test.Feature"), Feature.Owner, Found));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
