#include "DocKnowledgeSubsystem.h"
#include "DocKnowledgeCodexLog.h"
#include "DocCoreTags.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Internationalization/Internationalization.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocKnowledgeSubsystem)

static TWeakObjectPtr<UDocKnowledgeSubsystem> GDocKnowledgeTestOverride;

namespace DocKnowledgePrivate
{
	void DefaultDispatch(UDocKnowledgeSubsystem::FSearchWork Work, UDocKnowledgeSubsystem::FSearchApply Apply)
	{
		// Work reads only an immutable copied index; the result is applied on the game thread.
		Async(EAsyncExecution::ThreadPool, [Work = MoveTemp(Work), Apply = MoveTemp(Apply)]() mutable
		{
			FDocKnowledgeSearchPage Page = Work();
			AsyncTask(ENamedThreads::GameThread, [Apply = MoveTemp(Apply), Page = MoveTemp(Page)]() mutable
			{
				Apply(MoveTemp(Page));
			});
		});
	}

	bool SectionVisible(const FDocKnowledgeSection& S, const FDocKnowledgeRuntimeState& State)
	{
		return (S.RevealStage > 0 && State.Stage >= S.RevealStage) || State.RevealedSections.Contains(S.SectionId);
	}
}

using namespace DocKnowledgePrivate;

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

UDocKnowledgeSubsystem* UDocKnowledgeSubsystem::Get(const UObject* WorldContextObject)
{
	if (UDocKnowledgeSubsystem* Override = GDocKnowledgeTestOverride.Get())
	{
		return Override;
	}
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UDocKnowledgeSubsystem>() : nullptr;
}

void UDocKnowledgeSubsystem::SetSubsystemOverrideForTesting(UDocKnowledgeSubsystem* Override)
{
	GDocKnowledgeTestOverride = Override;
}

void UDocKnowledgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CultureChangedHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &UDocKnowledgeSubsystem::HandleCultureChanged);
}

void UDocKnowledgeSubsystem::Deinitialize()
{
	FInternationalization::Get().OnCultureChanged().Remove(CultureChangedHandle);
	TArray<FDocRequestHandle> Pending;
	Searches.ForEach([&Pending](const FDocRequestHandle& H, const FSearchRecord&) { Pending.Add(H); });
	for (const FDocRequestHandle& H : Pending)
	{
		CancelSearch(H);
	}
	IndexCache.Reset();
	Super::Deinitialize();
}

const UDocKnowledgeSettings* UDocKnowledgeSubsystem::Settings() const
{
	return GetDefault<UDocKnowledgeSettings>();
}

void UDocKnowledgeSubsystem::HandleCultureChanged()
{
	++CultureGeneration; // culture-dependent search/sort caches are rebuilt; record ids are untouched
	IndexCache.Reset();
}

// ---------------------------------------------------------------------------
// Catalog
// ---------------------------------------------------------------------------

FDocSystemResult UDocKnowledgeSubsystem::RegisterEntry(UDocKnowledgeEntry* Entry)
{
	if (!Entry)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No entry"));
	}
	TArray<FString> Errors, Warnings;
	Entry->FindProblems(Errors, Warnings);
	if (!Errors.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
			FString::Printf(TEXT("Entry %s is invalid: %s"), *Entry->EntryId.ToString(), *FString::Join(Errors, TEXT("; "))));
	}
	if (const TObjectPtr<UDocKnowledgeEntry>* Existing = Entries.Find(Entry->EntryId))
	{
		return *Existing == Entry ? FDocSystemResult::MakeNoChange(TEXT("Already registered"))
			: FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("EntryId %s already registered"), *Entry->EntryId.ToString()));
	}
	Entries.Add(Entry->EntryId, Entry);
	EntryRefs.Add(Entry);
	++CatalogGeneration;
	IndexCache.Reset();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocKnowledgeSubsystem::RegisterCatalog(UDocKnowledgeCatalog* Catalog)
{
	if (!Catalog)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No catalog"));
	}
	TArray<FString> Failures;
	for (UDocKnowledgeEntry* Entry : Catalog->Entries)
	{
		const FDocSystemResult R = RegisterEntry(Entry);
		if (!R.IsSuccess())
		{
			Failures.Add(R.Diagnostic);
		}
	}
	Redirects.Append(Catalog->EntryRedirects);
	++CatalogGeneration;
	IndexCache.Reset();
	return Failures.IsEmpty() ? FDocSystemResult::MakeSuccess()
		: FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Join(Failures, TEXT("; ")));
}

void UDocKnowledgeSubsystem::ValidateCatalog(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const
{
	for (const TPair<FName, TObjectPtr<UDocKnowledgeEntry>>& Pair : Entries)
	{
		Pair.Value->FindProblems(OutErrors, OutWarnings);
		for (const FDocKnowledgeRelation& R : Pair.Value->Relations)
		{
			if (!Entries.Contains(R.TargetEntryId))
			{
				OutWarnings.Add(FString::Printf(TEXT("Entry %s: dangling relation to %s (hidden from player queries)"), *Pair.Key.ToString(), *R.TargetEntryId.ToString()));
			}
		}
	}
	for (const TPair<FName, FName>& Redirect : Redirects)
	{
		TSet<FName> Seen = { Redirect.Key };
		FName Cursor = Redirect.Value;
		bool bCycle = false;
		while (true)
		{
			bool bAgain = false;
			Seen.Add(Cursor, &bAgain);
			if (bAgain) { bCycle = true; break; }
			const FName* Next = Redirects.Find(Cursor);
			if (!Next) { break; }
			Cursor = *Next;
		}
		if (bCycle)
		{
			OutErrors.Add(FString::Printf(TEXT("Redirect cycle through %s"), *Redirect.Key.ToString()));
		}
		else if (!Entries.Contains(Cursor))
		{
			OutWarnings.Add(FString::Printf(TEXT("Redirect %s -> %s targets no registered entry"), *Redirect.Key.ToString(), *Cursor.ToString()));
		}
	}
}

const UDocKnowledgeEntry* UDocKnowledgeSubsystem::FindEntry(FName EntryId) const
{
	const TObjectPtr<UDocKnowledgeEntry>* Found = Entries.Find(EntryId);
	return Found ? Found->Get() : nullptr;
}

// ---------------------------------------------------------------------------
// Owners and state
// ---------------------------------------------------------------------------

UDocKnowledgeSubsystem::FOwnerData& UDocKnowledgeSubsystem::GetOwnerData(const FDocOwnerScope& Owner)
{
	FOwnerData& Data = Owners.FindOrAdd(Owner);
	if (!Data.Ledger.IsValid())
	{
		Data.Ledger = MakeShared<FDocReceiptLedger>(Settings()->MaxReceiptsPerOwner);
	}
	return Data;
}

const FDocKnowledgeRuntimeState* UDocKnowledgeSubsystem::FindState(const FDocOwnerScope& Owner, FName EntryId) const
{
	const FOwnerData* Data = Owners.Find(Owner);
	return Data ? Data->Entries.Find(EntryId) : nullptr;
}

FDocKnowledgeRuntimeState& UDocKnowledgeSubsystem::EditState(const FDocOwnerScope& Owner, FName EntryId)
{
	FDocKnowledgeRuntimeState& State = GetOwnerData(Owner).Entries.FindOrAdd(EntryId);
	State.EntryId = EntryId;
	return State;
}

FDocKnowledgeRuntimeState UDocKnowledgeSubsystem::Combined(const FDocOwnerScope& Owner, FName EntryId) const
{
	FDocKnowledgeRuntimeState Out;
	Out.EntryId = EntryId;
	if (const FDocKnowledgeRuntimeState* Own = FindState(Owner, EntryId))
	{
		Out = *Own;
	}
	const FDocOwnerScope* SharedScope = SharedLinks.Find(Owner);
	if (const FDocKnowledgeRuntimeState* S = SharedScope ? FindState(*SharedScope, EntryId) : nullptr)
	{
		// Discovery is shared; read state stays the player's own.
		Out.Stage = FMath::Max(Out.Stage, S->Stage);
		for (FName Section : S->RevealedSections) { Out.RevealedSections.AddUnique(Section); }
		Out.bHidden |= S->bHidden;
		Out.bCompleted |= S->bCompleted;
		Out.bQuarantined |= S->bQuarantined;
		Out.VisibleRevision += S->VisibleRevision;
		Out.UpdateCount += S->UpdateCount;
		Out.DiscoveryTimeUtcTicks = Out.DiscoveryTimeUtcTicks == 0 ? S->DiscoveryTimeUtcTicks
			: (S->DiscoveryTimeUtcTicks == 0 ? Out.DiscoveryTimeUtcTicks : FMath::Min(Out.DiscoveryTimeUtcTicks, S->DiscoveryTimeUtcTicks));
		Out.LastUpdateTimeUtcTicks = FMath::Max(Out.LastUpdateTimeUtcTicks, S->LastUpdateTimeUtcTicks);
		Out.ContentRevisionSeen = FMath::Max(Out.ContentRevisionSeen, S->ContentRevisionSeen);
	}
	return Out;
}

bool UDocKnowledgeSubsystem::IsVisible(const FDocKnowledgeRuntimeState& State, const UDocKnowledgeEntry& Entry) const
{
	return !State.bQuarantined && !State.bHidden && State.Stage >= FMath::Max(1, Entry.TitleStage);
}

bool UDocKnowledgeSubsystem::IsPlaceholder(const FDocKnowledgeRuntimeState& State, const UDocKnowledgeEntry& Entry) const
{
	return !IsVisible(State, Entry) && !State.bHidden && !State.bQuarantined && !Entry.bHiddenUntilDiscovered;
}

void UDocKnowledgeSubsystem::Touch(const FDocOwnerScope& Owner)
{
	++GetOwnerData(Owner).Revision; // invalidates this owner's (and linked players') visible index
}

void UDocKnowledgeSubsystem::LinkSharedScope(const FDocOwnerScope& Player, const FDocOwnerScope& Shared)
{
	if (Player.IsValid() && Shared.IsValid() && Player != Shared)
	{
		SharedLinks.Add(Player, Shared);
		Touch(Player);
	}
}

void UDocKnowledgeSubsystem::UnlinkSharedScope(const FDocOwnerScope& Player)
{
	if (SharedLinks.Remove(Player) > 0)
	{
		Touch(Player);
	}
}

void UDocKnowledgeSubsystem::RemoveOwner(const FDocOwnerScope& Owner)
{
	CancelSearchesFor(Owner);
	IndexCache.Remove(Owner);
}

FDocSystemResult UDocKnowledgeSubsystem::Validate(const FDocOwnerScope& Owner, FName EntryId, const UDocKnowledgeEntry*& OutEntry) const
{
	if (!Owner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("A valid owner scope is required"));
	}
	OutEntry = FindEntry(EntryId);
	if (!OutEntry)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Unknown entry %s"), *EntryId.ToString()), DocKnowledgeTags::Error_Knowledge_UnknownEntry);
	}
	if (const FDocKnowledgeRuntimeState* State = FindState(Owner, EntryId))
	{
		if (State->bQuarantined)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Entry state is quarantined"), DocKnowledgeTags::Error_Knowledge_Quarantined);
		}
	}
	return FDocSystemResult::MakeSuccess();
}

bool UDocKnowledgeSubsystem::CheckGrant(const FDocOwnerScope& Owner, const FDocKnowledgeGrant& Grant, int64 PayloadHash, FDocSystemResult& OutPrior) const
{
	if (!Grant.EffectKey.IsValid())
	{
		return false;
	}
	const FOwnerData* Data = Owners.Find(Owner);
	if (!Data || !Data->Ledger.IsValid())
	{
		return false;
	}
	FDocEffectReceipt Existing;
	switch (Data->Ledger->Check(Grant.EffectKey, PayloadHash, &Existing))
	{
	case EDocReceiptCheck::Duplicate:
		OutPrior = Existing.Result; // the original result, not a new mutation
		return true;
	case EDocReceiptCheck::Conflict:
		OutPrior = FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("Effect key %s reused with a different payload"), *Grant.EffectKey.ToString()));
		return true;
	default:
		return false;
	}
}

void UDocKnowledgeSubsystem::RecordGrant(const FDocOwnerScope& Owner, const FDocKnowledgeGrant& Grant, int64 PayloadHash, const FDocSystemResult& Result)
{
	if (!Grant.EffectKey.IsValid() || !Result.IsSuccess())
	{
		return; // failures are not receipted: a retry may succeed
	}
	FDocEffectReceipt Receipt;
	Receipt.Key = Grant.EffectKey;
	Receipt.PayloadHash = PayloadHash;
	Receipt.Result = Result;
	Receipt.CommittedRevision = GetOwnerData(Owner).Revision;
	GetOwnerData(Owner).Ledger->Record(Receipt);
}

void UDocKnowledgeSubsystem::Notify(FDocKnowledgeChangeEvent& Dynamic, FDocKnowledgeChangeNative& Native, const FDocOwnerScope& Owner, const UDocKnowledgeEntry& Entry, int64 OldRevision, const FString& Cause)
{
	const FDocKnowledgeRuntimeState* Own = FindState(Owner, Entry.EntryId);
	const FDocKnowledgeRuntimeState View = Combined(Owner, Entry.EntryId);
	FDocKnowledgeChange Change;
	Change.Owner = Owner;
	Change.EntryId = Entry.EntryId;
	Change.OldRevision = OldRevision;
	Change.NewRevision = Own ? Own->VisibleRevision : 0;
	Change.Stage = View.Stage;
	Change.Cause = Cause;
	if (IsVisible(View, Entry))
	{
		Change.VisibleTitle = Entry.Title; // only visibility-safe text
	}
	Native.Broadcast(Change);
	Dynamic.Broadcast(Change);
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

FDocSystemResult UDocKnowledgeSubsystem::DiscoverEntry(const FDocOwnerScope& Owner, FName EntryId, const FDocKnowledgeGrant& Grant)
{
	const UDocKnowledgeEntry* Entry = FindEntry(EntryId);
	return RevealEntry(Owner, EntryId, Entry ? FMath::Max(1, Entry->TitleStage) : 1, Grant);
}

FDocSystemResult UDocKnowledgeSubsystem::RevealEntry(const FDocOwnerScope& Owner, FName EntryId, int32 Stage, const FDocKnowledgeGrant& Grant)
{
	const UDocKnowledgeEntry* Entry = nullptr;
	FDocSystemResult Check = Validate(Owner, EntryId, Entry);
	if (!Check.IsSuccess())
	{
		return Check;
	}
	Stage = FMath::Clamp(Stage, 1, Entry->MaxStage);
	const int64 Hash = FDocReceiptLedger::HashString(FString::Printf(TEXT("Reveal|%s|%d"), *EntryId.ToString(), Stage));
	FDocSystemResult Prior;
	if (CheckGrant(Owner, Grant, Hash, Prior))
	{
		return Prior;
	}

	FDocKnowledgeRuntimeState& State = EditState(Owner, EntryId);
	if (State.Stage >= Stage)
	{
		// Monotonic by default: no update count, no discovery replay.
		const FDocSystemResult Same = FDocSystemResult::MakeNoChange(TEXT("Already revealed"));
		RecordGrant(Owner, Grant, Hash, Same);
		return Same;
	}
	const bool bFirst = State.Stage == 0;
	const int64 OldRevision = State.VisibleRevision;
	const int64 Now = NowUtc();
	State.Stage = Stage;
	++State.VisibleRevision;
	if (bFirst)
	{
		State.DiscoveryTimeUtcTicks = Now;
	}
	else if (Settings()->UpdateCountPolicy == EDocKnowledgeUpdateCountPolicy::RevealsAndSections)
	{
		++State.UpdateCount;
	}
	State.LastUpdateTimeUtcTicks = Now;
	State.ContentRevisionSeen = Entry->ContentRevision;
	if (Entry->bCompleteAtMaxStage && Stage >= Entry->MaxStage)
	{
		State.bCompleted = true;
	}
	Touch(Owner);
	const FDocSystemResult Result = FDocSystemResult::MakeSuccess();
	RecordGrant(Owner, Grant, Hash, Result);
	const FString Cause = Grant.Cause.IsEmpty() ? Grant.SourceId.ToString() : Grant.Cause;
	if (bFirst)
	{
		Notify(OnEntryDiscovered, OnEntryDiscoveredNative, Owner, *Entry, OldRevision, Cause);
	}
	else
	{
		Notify(OnEntryUpdated, OnEntryUpdatedNative, Owner, *Entry, OldRevision, Cause);
	}
	return Result;
}

FDocSystemResult UDocKnowledgeSubsystem::UpdateEntry(const FDocOwnerScope& Owner, FName EntryId, FName SectionId, const FDocKnowledgeGrant& Grant)
{
	const UDocKnowledgeEntry* Entry = nullptr;
	FDocSystemResult Check = Validate(Owner, EntryId, Entry);
	if (!Check.IsSuccess())
	{
		return Check;
	}
	const FDocKnowledgeSection* Section = Entry->FindSection(SectionId);
	if (!Section)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Entry %s has no section %s"), *EntryId.ToString(), *SectionId.ToString()));
	}
	if (!Entry->bCanUpdate)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Entry does not accept updates"), DocKnowledgeTags::Error_Knowledge_CannotUpdate);
	}
	const FDocKnowledgeRuntimeState View = Combined(Owner, EntryId);
	if (!View.IsDiscovered())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Entry is not discovered"), DocKnowledgeTags::Error_Knowledge_NotDiscovered);
	}
	const int64 Hash = FDocReceiptLedger::HashString(FString::Printf(TEXT("Section|%s|%s"), *EntryId.ToString(), *SectionId.ToString()));
	FDocSystemResult Prior;
	if (CheckGrant(Owner, Grant, Hash, Prior))
	{
		return Prior;
	}
	if (SectionVisible(*Section, View))
	{
		const FDocSystemResult Same = FDocSystemResult::MakeNoChange(TEXT("Section already visible"));
		RecordGrant(Owner, Grant, Hash, Same);
		return Same;
	}
	FDocKnowledgeRuntimeState& State = EditState(Owner, EntryId);
	const int64 OldRevision = State.VisibleRevision;
	State.RevealedSections.AddUnique(SectionId);
	++State.VisibleRevision;
	++State.UpdateCount;
	State.LastUpdateTimeUtcTicks = NowUtc();
	Touch(Owner);
	const FDocSystemResult Result = FDocSystemResult::MakeSuccess();
	RecordGrant(Owner, Grant, Hash, Result);
	Notify(OnEntryUpdated, OnEntryUpdatedNative, Owner, *Entry, OldRevision, Grant.Cause.IsEmpty() ? FString::Printf(TEXT("Section %s"), *SectionId.ToString()) : Grant.Cause);
	return Result;
}

FDocSystemResult UDocKnowledgeSubsystem::MarkRead(const FDocOwnerScope& Owner, FName EntryId, int64 DisplayedRevision)
{
	const UDocKnowledgeEntry* Entry = nullptr;
	FDocSystemResult Check = Validate(Owner, EntryId, Entry);
	if (!Check.IsSuccess())
	{
		return Check;
	}
	const FDocKnowledgeRuntimeState View = Combined(Owner, EntryId);
	if (!IsVisible(View, *Entry))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Entry is not visible"), DocKnowledgeTags::Error_Knowledge_NotDiscovered);
	}
	if (DisplayedRevision <= 0 || DisplayedRevision > View.VisibleRevision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
			FString::Printf(TEXT("Revision %lld was never displayed (current %lld)"), DisplayedRevision, View.VisibleRevision), DocKnowledgeTags::Error_Knowledge_StaleRevision);
	}
	if (DisplayedRevision <= View.ReadRevision)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already read"));
	}
	FDocKnowledgeRuntimeState& State = EditState(Owner, EntryId);
	const int64 OldRead = State.ReadRevision;
	State.ReadRevision = DisplayedRevision; // a later unseen update stays "Updated"
	State.ReadTimeUtcTicks = NowUtc();
	++GetOwnerData(Owner).Revision;
	Notify(OnEntryRead, OnEntryReadNative, Owner, *Entry, OldRead, TEXT("Read"));
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocKnowledgeSubsystem::CompleteEntry(const FDocOwnerScope& Owner, FName EntryId, const FDocKnowledgeGrant& Grant)
{
	const UDocKnowledgeEntry* Entry = nullptr;
	FDocSystemResult Check = Validate(Owner, EntryId, Entry);
	if (!Check.IsSuccess())
	{
		return Check;
	}
	if (!Combined(Owner, EntryId).IsDiscovered())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Entry is not discovered"), DocKnowledgeTags::Error_Knowledge_NotDiscovered);
	}
	const int64 Hash = FDocReceiptLedger::HashString(FString::Printf(TEXT("Complete|%s"), *EntryId.ToString()));
	FDocSystemResult Prior;
	if (CheckGrant(Owner, Grant, Hash, Prior))
	{
		return Prior;
	}
	FDocKnowledgeRuntimeState& State = EditState(Owner, EntryId);
	if (State.bCompleted)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already completed"));
	}
	State.bCompleted = true;
	Touch(Owner);
	const FDocSystemResult Result = FDocSystemResult::MakeSuccess();
	RecordGrant(Owner, Grant, Hash, Result);
	Notify(OnEntryUpdated, OnEntryUpdatedNative, Owner, *Entry, State.VisibleRevision, TEXT("Completed"));
	return Result;
}

FDocSystemResult UDocKnowledgeSubsystem::ConcealEntry(const FDocOwnerScope& Owner, FName EntryId, bool bHidden)
{
	const UDocKnowledgeEntry* Entry = nullptr;
	FDocSystemResult Check = Validate(Owner, EntryId, Entry);
	if (!Check.IsSuccess())
	{
		return Check;
	}
	FDocKnowledgeRuntimeState& State = EditState(Owner, EntryId);
	if (State.bHidden == bHidden)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Visibility unchanged"));
	}
	State.bHidden = bHidden; // discovery history (stage, times, reads) is kept
	Touch(Owner);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocKnowledgeSubsystem::RevokeReveal(const FDocOwnerScope& Owner, FName EntryId, int32 NewStage, bool bClearCompletion)
{
	const UDocKnowledgeEntry* Entry = nullptr;
	FDocSystemResult Check = Validate(Owner, EntryId, Entry);
	if (!Check.IsSuccess())
	{
		return Check;
	}
	FDocKnowledgeRuntimeState& State = EditState(Owner, EntryId);
	NewStage = FMath::Max(0, NewStage);
	if (NewStage >= State.Stage)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Nothing to revoke"));
	}
	const int64 OldRevision = State.VisibleRevision;
	const bool bWasFullyRead = State.ReadRevision >= Combined(Owner, EntryId).VisibleRevision;
	State.Stage = NewStage;
	if (NewStage == 0)
	{
		State.RevealedSections.Reset();
	}
	++State.VisibleRevision;
	if (bClearCompletion)
	{
		State.bCompleted = false;
	}
	if (bWasFullyRead)
	{
		State.ReadRevision = Combined(Owner, EntryId).VisibleRevision; // removed content is not "unread"
	}
	Touch(Owner);
	Notify(OnEntryUpdated, OnEntryUpdatedNative, Owner, *Entry, OldRevision, TEXT("Revoked"));
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocKnowledgeSubsystem::SetEntryState(const FDocOwnerScope& Owner, FName EntryId, EDocKnowledgeDisplayLabel Label)
{
	const UDocKnowledgeEntry* Entry = nullptr;
	FDocSystemResult Check = Validate(Owner, EntryId, Entry);
	if (!Check.IsSuccess())
	{
		return Check;
	}
	const FDocKnowledgeRuntimeState View = Combined(Owner, EntryId);
	const FDocKnowledgeGrant Admin;
	switch (Label)
	{
	case EDocKnowledgeDisplayLabel::Unknown:
		return View.IsDiscovered()
			? FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Discovered -> Unknown is not a legal transition; use RevokeReveal"), DocKnowledgeTags::Error_Knowledge_IllegalTransition)
			: FDocSystemResult::MakeNoChange(TEXT("Already unknown"));
	case EDocKnowledgeDisplayLabel::Discovered:
		if (View.bHidden)
		{
			return ConcealEntry(Owner, EntryId, false);
		}
		return DiscoverEntry(Owner, EntryId, Admin);
	case EDocKnowledgeDisplayLabel::Updated:
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Updated is derived from content; use UpdateEntry"), DocKnowledgeTags::Error_Knowledge_IllegalTransition);
	case EDocKnowledgeDisplayLabel::Read:
		return MarkRead(Owner, EntryId, View.VisibleRevision);
	case EDocKnowledgeDisplayLabel::Completed:
		return CompleteEntry(Owner, EntryId, Admin);
	case EDocKnowledgeDisplayLabel::Hidden:
		return ConcealEntry(Owner, EntryId, true);
	}
	return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Unknown label"));
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool UDocKnowledgeSubsystem::IsDiscovered(const FDocOwnerScope& Owner, FName EntryId) const
{
	const FDocKnowledgeRuntimeState View = Combined(Owner, EntryId);
	return View.IsDiscovered() && !View.bQuarantined; // a discovered fact, independent of widgets or visibility
}

FDocKnowledgeRuntimeState UDocKnowledgeSubsystem::GetEntryState(const FDocOwnerScope& Owner, FName EntryId) const
{
	return Combined(Owner, EntryId);
}

TArray<FName> UDocKnowledgeSubsystem::VisibleRelationTargets(const FDocOwnerScope& Owner, const UDocKnowledgeEntry& Entry, const FDocKnowledgeRuntimeState& State) const
{
	TArray<FName> Out;
	for (const FDocKnowledgeRelation& R : Entry.Relations)
	{
		if (State.Stage < R.RevealStage)
		{
			continue;
		}
		const UDocKnowledgeEntry* Target = FindEntry(R.TargetEntryId);
		if (!Target || !IsVisible(Combined(Owner, R.TargetEntryId), *Target))
		{
			continue; // dangling or undiscovered: never exposed
		}
		Out.AddUnique(R.TargetEntryId);
	}
	return Out;
}

bool UDocKnowledgeSubsystem::BuildVisibleEntry(const FDocOwnerScope& Owner, const UDocKnowledgeEntry& Entry, FDocKnowledgeVisibleEntry& Out) const
{
	const FDocKnowledgeRuntimeState State = Combined(Owner, Entry.EntryId);
	const bool bVisible = IsVisible(State, Entry);
	if (!bVisible && !IsPlaceholder(State, Entry))
	{
		return false;
	}
	Out = FDocKnowledgeVisibleEntry();
	Out.EntryId = Entry.EntryId;
	Out.Category = Entry.Category;
	Out.SortOrder = Entry.SortOrder;
	Out.bPlaceholder = !bVisible;
	Out.Stage = bVisible ? State.Stage : 0;
	Out.Label = State.GetDisplayLabel();
	Out.VisibleRevision = State.VisibleRevision;
	Out.ReadRevision = State.ReadRevision;
	if (Entry.StageLabels.IsValidIndex(Out.Stage))
	{
		Out.StageLabel = Entry.StageLabels[Out.Stage];
	}
	if (!bVisible)
	{
		return true; // placeholder: no text, tags, media or links
	}
	Out.Title = Entry.Title;
	if (State.Stage >= Entry.SummaryStage) { Out.Summary = Entry.Summary; }
	if (State.Stage >= Entry.BodyStage) { Out.Body = Entry.Body; }
	Out.Tags = Entry.Tags;
	for (const FDocKnowledgeSection& S : Entry.Sections)
	{
		if (SectionVisible(S, State))
		{
			FDocKnowledgeSectionView& V = Out.Sections.AddDefaulted_GetRef();
			V.SectionId = S.SectionId;
			V.Heading = S.Heading;
			V.Body = S.Body;
			V.Revision = S.Revision;
		}
	}
	for (const FDocKnowledgeMedia& M : Entry.Media)
	{
		if (State.Stage < M.RevealStage)
		{
			continue;
		}
		FDocKnowledgeMediaView& V = Out.Media.AddDefaulted_GetRef();
		V.MediaId = M.MediaId;
		V.Type = M.Type;
		V.Asset = M.Asset;
		V.Caption = M.Caption;
		V.Transcript = M.Transcript;
		// External sources resolve only through a configured provider; the base never opens them.
		V.bAvailable = MediaProvider.IsValid() ? MediaProvider->IsMediaAvailable(M)
			: (M.Type != EDocKnowledgeMediaType::External && M.Asset.IsValid());
		V.FallbackText = !M.Transcript.IsEmpty() ? M.Transcript : (!M.Caption.IsEmpty() ? M.Caption : Entry.Title);
	}
	for (const FDocKnowledgeRelation& R : Entry.Relations)
	{
		if (State.Stage < R.RevealStage)
		{
			continue;
		}
		const UDocKnowledgeEntry* Target = FindEntry(R.TargetEntryId);
		if (!Target || !IsVisible(Combined(Owner, R.TargetEntryId), *Target))
		{
			continue;
		}
		FDocKnowledgeRelationView& V = Out.Relations.AddDefaulted_GetRef();
		V.Type = R.Type;
		V.CustomType = R.CustomType;
		V.TargetEntryId = R.TargetEntryId;
		V.TargetTitle = Target->Title;
	}
	return true;
}

bool UDocKnowledgeSubsystem::GetVisibleEntry(const FDocOwnerScope& Owner, FName EntryId, FDocKnowledgeVisibleEntry& OutEntry) const
{
	const UDocKnowledgeEntry* Entry = FindEntry(EntryId);
	return Entry && BuildVisibleEntry(Owner, *Entry, OutEntry);
}

TArray<FName> UDocKnowledgeSubsystem::GetEntriesByCategory(const FDocOwnerScope& Owner, FGameplayTag Category, bool bIncludeChildren) const
{
	TArray<const UDocKnowledgeEntry*> Found;
	for (const TPair<FName, TObjectPtr<UDocKnowledgeEntry>>& Pair : Entries)
	{
		const UDocKnowledgeEntry* E = Pair.Value;
		const bool bInCategory = !Category.IsValid() || (bIncludeChildren ? E->Category.MatchesTag(Category) : E->Category == Category);
		const FDocKnowledgeRuntimeState State = Combined(Owner, Pair.Key);
		if (bInCategory && (IsVisible(State, *E) || IsPlaceholder(State, *E)))
		{
			Found.Add(E);
		}
	}
	Found.Sort([](const UDocKnowledgeEntry& A, const UDocKnowledgeEntry& B)
	{
		return A.SortOrder != B.SortOrder ? A.SortOrder < B.SortOrder : A.EntryId.LexicalLess(B.EntryId);
	});
	TArray<FName> Out;
	for (const UDocKnowledgeEntry* E : Found) { Out.Add(E->EntryId); }
	return Out;
}

TArray<FName> UDocKnowledgeSubsystem::GetEntriesByTag(const FDocOwnerScope& Owner, FGameplayTag Tag) const
{
	TArray<const UDocKnowledgeEntry*> Found;
	for (const TPair<FName, TObjectPtr<UDocKnowledgeEntry>>& Pair : Entries)
	{
		const UDocKnowledgeEntry* E = Pair.Value;
		if (E->Tags.HasTag(Tag) && IsVisible(Combined(Owner, Pair.Key), *E)) // tags are hidden until discovery
		{
			Found.Add(E);
		}
	}
	Found.Sort([](const UDocKnowledgeEntry& A, const UDocKnowledgeEntry& B)
	{
		return A.SortOrder != B.SortOrder ? A.SortOrder < B.SortOrder : A.EntryId.LexicalLess(B.EntryId);
	});
	TArray<FName> Out;
	for (const UDocKnowledgeEntry* E : Found) { Out.Add(E->EntryId); }
	return Out;
}

TArray<FName> UDocKnowledgeSubsystem::GetRelatedVisibleEntries(const FDocOwnerScope& Owner, FName EntryId, int32 MaxDepth, int32 MaxResults) const
{
	TArray<FName> Results;
	const UDocKnowledgeEntry* Start = FindEntry(EntryId);
	if (!Start || !IsVisible(Combined(Owner, EntryId), *Start))
	{
		return Results;
	}
	MaxDepth = FMath::Clamp(MaxDepth, 1, Settings()->MaxRelationDepth);
	MaxResults = FMath::Clamp(MaxResults, 1, Settings()->MaxRelationResults);
	TSet<FName> Visited = { EntryId };
	TArray<TPair<FName, int32>> Queue = { TPair<FName, int32>(EntryId, 0) };
	for (int32 Head = 0; Head < Queue.Num() && Results.Num() < MaxResults; ++Head)
	{
		const FName Current = Queue[Head].Key;
		const int32 Depth = Queue[Head].Value;
		const UDocKnowledgeEntry* Entry = FindEntry(Current);
		if (!Entry)
		{
			continue;
		}
		for (FName Target : VisibleRelationTargets(Owner, *Entry, Combined(Owner, Current)))
		{
			bool bSeen = false;
			Visited.Add(Target, &bSeen);
			if (bSeen)
			{
				continue; // cycles are legal relationships; traversal is protected
			}
			Results.Add(Target);
			if (Results.Num() >= MaxResults)
			{
				break;
			}
			if (Depth + 1 < MaxDepth)
			{
				Queue.Add(TPair<FName, int32>(Target, Depth + 1));
			}
		}
	}
	return Results;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

UDocKnowledgeSubsystem::FIndexKey UDocKnowledgeSubsystem::CurrentKey(const FDocOwnerScope& Owner) const
{
	FIndexKey Key;
	if (const FOwnerData* Data = Owners.Find(Owner)) { Key.OwnerRevision = Data->Revision; }
	if (const FDocOwnerScope* Shared = SharedLinks.Find(Owner))
	{
		if (const FOwnerData* Data = Owners.Find(*Shared)) { Key.SharedRevision = Data->Revision; }
		Key.SharedRevision += 1; // linked vs. unlinked differ even at revision 0
	}
	Key.Culture = CultureGeneration;
	Key.Catalog = CatalogGeneration;
	return Key;
}

TSharedPtr<const UDocKnowledgeSubsystem::FVisibleIndex> UDocKnowledgeSubsystem::GetIndex(const FDocOwnerScope& Owner)
{
	const FIndexKey Key = CurrentKey(Owner);
	if (const TSharedPtr<const FVisibleIndex>* Cached = IndexCache.Find(Owner))
	{
		if (Cached->IsValid() && (*Cached)->Key == Key)
		{
			return *Cached;
		}
	}
	++IndexBuildCount;
	TSharedRef<FVisibleIndex> Index = MakeShared<FVisibleIndex>();
	Index->Key = Key;

	struct FSortItem
	{
		int32 Slot = 0;
		int32 SortOrder = 0;
		bool bPlaceholder = false;
		FText Title;
		FName Id;
	};
	TArray<FSortItem> Order;
	for (const TPair<FName, TObjectPtr<UDocKnowledgeEntry>>& Pair : Entries)
	{
		const UDocKnowledgeEntry& E = *Pair.Value;
		const FDocKnowledgeRuntimeState State = Combined(Owner, Pair.Key);
		const bool bVisible = IsVisible(State, E);
		if (!bVisible && !IsPlaceholder(State, E))
		{
			continue; // undiscovered content never enters the index
		}
		FIndexedEntry I;
		I.EntryId = E.EntryId;
		I.Category = E.Category;
		I.CategoryWithParents = E.Category.IsValid() ? E.Category.GetGameplayTagParents() : FGameplayTagContainer();
		I.bPlaceholder = !bVisible;
		if (bVisible)
		{
			I.Tags = E.Tags;
			// Culture-aware lowercasing of visible text snapshots only.
			I.TitleLower = E.Title.ToLower().ToString();
			if (State.Stage >= E.SummaryStage) { I.SummaryLower = E.Summary.ToLower().ToString(); }
			FString Body;
			if (State.Stage >= E.BodyStage) { Body = E.Body.ToLower().ToString(); }
			for (const FDocKnowledgeSection& S : E.Sections)
			{
				if (SectionVisible(S, State))
				{
					Body += TEXT("\n") + S.Heading.ToLower().ToString() + TEXT("\n") + S.Body.ToLower().ToString();
				}
			}
			I.BodyLower = MoveTemp(Body);
			I.VisibleRelations = VisibleRelationTargets(Owner, E, State);
		}
		FSortItem Item;
		Item.Slot = Index->Entries.Add(MoveTemp(I));
		Item.SortOrder = E.SortOrder;
		Item.bPlaceholder = !bVisible;
		Item.Title = bVisible ? E.Title : FText::GetEmpty();
		Item.Id = E.EntryId;
		Order.Add(Item);
	}
	Order.Sort([](const FSortItem& A, const FSortItem& B)
	{
		if (A.SortOrder != B.SortOrder) { return A.SortOrder < B.SortOrder; }
		if (A.bPlaceholder != B.bPlaceholder) { return !A.bPlaceholder; }
		const int32 ByTitle = A.Title.CompareTo(B.Title); // culture-aware collation
		return ByTitle != 0 ? ByTitle < 0 : A.Id.LexicalLess(B.Id);
	});
	TArray<FIndexedEntry> Sorted;
	Sorted.Reserve(Order.Num());
	for (int32 Rank = 0; Rank < Order.Num(); ++Rank)
	{
		FIndexedEntry& I = Index->Entries[Order[Rank].Slot];
		I.SortRank = Rank;
		Sorted.Add(MoveTemp(I));
	}
	Index->Entries = MoveTemp(Sorted);
	TSharedPtr<const FVisibleIndex> Result = Index;
	IndexCache.Add(Owner, Result);
	return Result;
}

FDocKnowledgeSearchPage UDocKnowledgeSubsystem::RunSearch(const FVisibleIndex& Index, const FDocKnowledgeSearchQuery& Query, int32 MaxPageSize)
{
	// Worker-safe: only copied strings and precomputed tag arrays.
	const FString Needle = Query.Text.TrimStartAndEnd().ToLower();
	const int32 PageSize = FMath::Clamp(Query.PageSize, 1, FMath::Max(1, MaxPageSize));
	const int32 Offset = FMath::Max(0, Query.Offset);
	FDocKnowledgeSearchPage Page;
	Page.Offset = Offset;
	int32 Total = 0;
	for (const FIndexedEntry& E : Index.Entries)
	{
		if (Query.Category.IsValid())
		{
			const bool bMatch = Query.bIncludeChildCategories ? E.CategoryWithParents.HasTagExact(Query.Category) : E.Category == Query.Category;
			if (!bMatch) { continue; }
		}
		if (!Query.RequiredTags.IsEmpty() && (E.bPlaceholder || !E.Tags.HasAll(Query.RequiredTags))) { continue; }
		if (!Query.RelatedTo.IsNone() && (E.bPlaceholder || !E.VisibleRelations.Contains(Query.RelatedTo))) { continue; }
		if (!Needle.IsEmpty())
		{
			if (E.bPlaceholder) { continue; }
			const bool bHit = E.TitleLower.Contains(Needle) || E.SummaryLower.Contains(Needle) || (Query.bSearchBody && E.BodyLower.Contains(Needle));
			if (!bHit) { continue; }
		}
		++Total;
		if (Total > Offset && Page.EntryIds.Num() < PageSize)
		{
			Page.EntryIds.Add(E.EntryId);
		}
	}
	Page.TotalMatches = Total; // counts only what this owner may see
	Page.bHasMore = Offset + Page.EntryIds.Num() < Total;
	return Page;
}

FDocKnowledgeSearchPage UDocKnowledgeSubsystem::SearchEntriesNow(const FDocOwnerScope& Owner, const FDocKnowledgeSearchQuery& Query)
{
	if (!Owner.IsValid())
	{
		return FDocKnowledgeSearchPage();
	}
	const TSharedPtr<const FVisibleIndex> Index = GetIndex(Owner);
	return RunSearch(*Index, Query, Settings()->MaxPageSize);
}

FDocRequestHandle UDocKnowledgeSubsystem::SearchEntries(const FDocOwnerScope& Owner, const FDocKnowledgeSearchQuery& Query, FDocKnowledgeSearchCallback OnComplete)
{
	if (!Owner.IsValid())
	{
		if (OnComplete)
		{
			OnComplete(FDocKnowledgeSearchPage(), FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("A valid owner scope is required")));
		}
		return FDocRequestHandle();
	}
	const TSharedPtr<const FVisibleIndex> Index = GetIndex(Owner);
	FSearchRecord Record;
	Record.Owner = Owner;
	Record.Key = Index->Key;
	Record.Callback = MoveTemp(OnComplete);
	const FDocRequestHandle Handle = Searches.Add(this, MoveTemp(Record));
	const int32 MaxPage = Settings()->MaxPageSize;
	FSearchWork Work = [Index, Query, MaxPage]() { return RunSearch(*Index, Query, MaxPage); };
	FSearchApply Apply = [WeakThis = TWeakObjectPtr<UDocKnowledgeSubsystem>(this), Handle](FDocKnowledgeSearchPage Page)
	{
		if (UDocKnowledgeSubsystem* This = WeakThis.Get())
		{
			This->CompleteSearch(Handle, MoveTemp(Page));
		}
	};
	if (SearchDispatcher)
	{
		SearchDispatcher(MoveTemp(Work), MoveTemp(Apply));
	}
	else
	{
		DefaultDispatch(MoveTemp(Work), MoveTemp(Apply));
	}
	return Handle;
}

void UDocKnowledgeSubsystem::CompleteSearch(FDocRequestHandle Handle, FDocKnowledgeSearchPage Page)
{
	FSearchRecord Record;
	if (!Searches.Remove(Handle, this, &Record))
	{
		return; // cancelled, superseded or owner removed: late completion is ignored
	}
	if (!(CurrentKey(Record.Owner) == Record.Key))
	{
		if (Record.Callback)
		{
			Record.Callback(FDocKnowledgeSearchPage(), FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
				TEXT("Visible content, culture or catalog changed; search again"), DocKnowledgeTags::Error_Knowledge_SearchStale));
		}
		return;
	}
	if (Record.Callback)
	{
		Record.Callback(Page, FDocSystemResult::MakeSuccess(Handle.GetOperationId()));
	}
}

void UDocKnowledgeSubsystem::CancelSearch(FDocRequestHandle Handle)
{
	FSearchRecord Record;
	if (Searches.Remove(Handle, this, &Record) && Record.Callback)
	{
		Record.Callback(FDocKnowledgeSearchPage(), FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("Search cancelled")));
	}
}

void UDocKnowledgeSubsystem::CancelSearchesFor(const FDocOwnerScope& Owner)
{
	TArray<FDocRequestHandle> Handles;
	Searches.ForEach([&Handles, &Owner](const FDocRequestHandle& H, const FSearchRecord& R)
	{
		if (R.Owner == Owner) { Handles.Add(H); }
	});
	for (const FDocRequestHandle& H : Handles)
	{
		CancelSearch(H);
	}
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

FDocKnowledgeSaveData UDocKnowledgeSubsystem::CaptureState() const
{
	FDocKnowledgeSaveData Data;
	for (const TPair<FDocOwnerScope, FOwnerData>& Pair : Owners)
	{
		if (!Pair.Key.IsPersistable())
		{
			continue;
		}
		FDocKnowledgeOwnerSave& Save = Data.Owners.AddDefaulted_GetRef();
		Save.Owner = Pair.Key;
		Pair.Value.Entries.GenerateValueArray(Save.Entries);
		Save.Entries.Sort([](const FDocKnowledgeRuntimeState& A, const FDocKnowledgeRuntimeState& B) { return A.EntryId.LexicalLess(B.EntryId); });
		if (Pair.Value.Ledger.IsValid())
		{
			Save.Receipts = Pair.Value.Ledger->GetAll();
		}
	}
	for (const TPair<FDocOwnerScope, FDocOwnerScope>& Link : SharedLinks)
	{
		if (Link.Key.IsPersistable() && Link.Value.IsPersistable())
		{
			FDocKnowledgeLink& L = Data.Links.AddDefaulted_GetRef();
			L.Player = Link.Key;
			L.Shared = Link.Value;
		}
	}
	return Data;
}

FDocSystemResult UDocKnowledgeSubsystem::RestoreState(const FDocKnowledgeSaveData& Data)
{
	if (Data.SchemaVersion <= 0 || Data.SchemaVersion > FDocKnowledgeSaveData::CurrentSchemaVersion)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Unsupported knowledge schema %d"), Data.SchemaVersion));
	}
	TSet<FDocOwnerScope> SeenOwners;
	for (const FDocKnowledgeOwnerSave& Save : Data.Owners)
	{
		bool bDup = false;
		SeenOwners.Add(Save.Owner, &bDup);
		if (!Save.Owner.IsPersistable() || bDup)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Owner missing, not persistable, or duplicated"));
		}
		TSet<FName> Ids;
		for (const FDocKnowledgeRuntimeState& S : Save.Entries)
		{
			bool bDupEntry = false;
			Ids.Add(S.EntryId, &bDupEntry);
			if (S.EntryId.IsNone() || bDupEntry || S.Stage < 0 || S.VisibleRevision < 0 || S.ReadRevision < 0)
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Malformed entry state %s"), *S.EntryId.ToString()));
			}
		}
	}

	auto Merge = [](FDocKnowledgeRuntimeState& Into, const FDocKnowledgeRuntimeState& From)
	{
		Into.Stage = FMath::Max(Into.Stage, From.Stage);
		Into.bHidden &= From.bHidden;
		Into.bCompleted |= From.bCompleted;
		Into.VisibleRevision = FMath::Max(Into.VisibleRevision, From.VisibleRevision);
		Into.ReadRevision = FMath::Max(Into.ReadRevision, From.ReadRevision);
		Into.UpdateCount = FMath::Max(Into.UpdateCount, From.UpdateCount);
		for (FName Section : From.RevealedSections) { Into.RevealedSections.AddUnique(Section); }
	};

	int32 Quarantined = 0;
	TMap<FDocOwnerScope, FOwnerData> Staged;
	for (const FDocKnowledgeOwnerSave& Save : Data.Owners)
	{
		FOwnerData& D = Staged.Add(Save.Owner);
		D.Ledger = MakeShared<FDocReceiptLedger>(Settings()->MaxReceiptsPerOwner);
		D.Ledger->RestoreAll(Save.Receipts);
		D.Revision = 1;
		for (FDocKnowledgeRuntimeState S : Save.Entries)
		{
			const UDocKnowledgeEntry* Entry = FindEntry(S.EntryId);
			if (!Entry)
			{
				FName Cursor = S.EntryId;
				for (int32 Hops = 0; Hops < 8 && !Entry; ++Hops)
				{
					const FName* Next = Redirects.Find(Cursor);
					if (!Next) { break; }
					Cursor = *Next;
					Entry = FindEntry(Cursor);
				}
				if (Entry)
				{
					S.EntryId = Cursor;
				}
			}
			if (!Entry)
			{
				// Tombstone: kept and saved again, hidden from player APIs, never inferred complete.
				S.bQuarantined = true;
				++Quarantined;
				D.Entries.Add(S.EntryId, S);
				continue;
			}
			S.bQuarantined = false;
			S.Stage = FMath::Clamp(S.Stage, 0, Entry->MaxStage);
			S.RevealedSections.RemoveAll([Entry](FName Id) { return !Entry->FindSection(Id); });
			if (S.IsDiscovered() && S.ContentRevisionSeen > 0 && S.ContentRevisionSeen < Entry->ContentRevision && Settings()->bFlagRevisedContentAsUpdated)
			{
				++S.VisibleRevision; // revised content shows as unread; no event, no update count
			}
			S.ContentRevisionSeen = S.IsDiscovered() ? Entry->ContentRevision : S.ContentRevisionSeen;
			if (FDocKnowledgeRuntimeState* Existing = D.Entries.Find(S.EntryId))
			{
				Merge(*Existing, S);
			}
			else
			{
				D.Entries.Add(S.EntryId, S);
			}
		}
	}

	TArray<FDocRequestHandle> Pending;
	Searches.ForEach([&Pending](const FDocRequestHandle& H, const FSearchRecord&) { Pending.Add(H); });
	for (const FDocRequestHandle& H : Pending)
	{
		CancelSearch(H); // results computed against pre-restore state are never applied
	}
	Owners = MoveTemp(Staged);
	SharedLinks.Reset();
	for (const FDocKnowledgeLink& L : Data.Links)
	{
		if (L.Player.IsValid() && L.Shared.IsValid())
		{
			SharedLinks.Add(L.Player, L.Shared);
		}
	}
	++CatalogGeneration;
	IndexCache.Reset();

	OnStateRefreshedNative.Broadcast(); // restore refreshes; it never replays discovery toasts
	OnStateRefreshed.Broadcast();
	FDocSystemResult Result = FDocSystemResult::MakeSuccess();
	Result.Diagnostic = FString::Printf(TEXT("%d owner(s), %d quarantined entry state(s)"), Owners.Num(), Quarantined);
	return Result;
}

#if !UE_BUILD_SHIPPING
FString UDocKnowledgeSubsystem::DebugDescribeOwner(const FDocOwnerScope& Owner) const
{
	const FIndexKey Key = CurrentKey(Owner);
	FString Out = FString::Printf(TEXT("Owner %s (revision %lld, shared %lld, culture %d, catalog %d)\n"),
		*Owner.ToString(), Key.OwnerRevision, Key.SharedRevision, Key.Culture, Key.Catalog);
	if (const FOwnerData* Data = Owners.Find(Owner))
	{
		for (const TPair<FName, FDocKnowledgeRuntimeState>& Pair : Data->Entries)
		{
			const FDocKnowledgeRuntimeState& S = Pair.Value;
			Out += FString::Printf(TEXT("  %s stage=%d rev=%lld read=%lld hidden=%d done=%d quarantined=%d\n"),
				*Pair.Key.ToString(), S.Stage, S.VisibleRevision, S.ReadRevision, S.bHidden ? 1 : 0, S.bCompleted ? 1 : 0, S.bQuarantined ? 1 : 0);
		}
	}
	return Out;
}
#endif
