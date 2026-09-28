#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Misc/DateTime.h"
#include "DocRequestHandle.h"
#include "DocKnowledgeTypes.h"
#include "DocKnowledgeSubsystem.generated.h"

/** Optional media adapter: decides availability of media descriptors (and is the only route for External sources). */
class DOCKNOWLEDGECODEXRUNTIME_API IDocKnowledgeMediaProvider
{
public:
	virtual ~IDocKnowledgeMediaProvider() = default;
	virtual bool IsMediaAvailable(const FDocKnowledgeMedia& Media) const = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocKnowledgeChangeEvent, const FDocKnowledgeChange&, Change);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDocKnowledgeRefreshEvent);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocKnowledgeChangeNative, const FDocKnowledgeChange&);

/** Search completion: exactly one call per request (result, Cancelled, or a stale Conflict). */
using FDocKnowledgeSearchCallback = TFunction<void(const FDocKnowledgeSearchPage&, const FDocSystemResult&)>;

/**
 * Knowledge for every owner scope in the game instance (KnowledgeSubsystem, handoff
 * Section 9). No UI dependency. Ordinary player APIs return only content visible to the
 * requesting owner: hidden titles, unrevealed text, media names, counts and links never
 * leak through queries or search. This is not protection against cooked-asset extraction.
 */
UCLASS()
class DOCKNOWLEDGECODEXRUNTIME_API UDocKnowledgeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UDocKnowledgeSubsystem* Get(const UObject* WorldContextObject);

	// ---- Catalog ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	FDocSystemResult RegisterEntry(UDocKnowledgeEntry* Entry);

	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	FDocSystemResult RegisterCatalog(UDocKnowledgeCatalog* Catalog);

	/** Editor/tool validation over the full catalog (dangling links, redirect cycles). */
	void ValidateCatalog(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const;

	const UDocKnowledgeEntry* FindEntry(FName EntryId) const;
	void SetMediaProvider(TSharedPtr<IDocKnowledgeMediaProvider> Provider) { MediaProvider = Provider; }

	// ---- Owners ----
	/** Player sees Shared's discoveries too; reads and completion stay per scope. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	void LinkSharedScope(const FDocOwnerScope& Player, const FDocOwnerScope& Shared);

	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	void UnlinkSharedScope(const FDocOwnerScope& Player);

	/** Player removal: cancels its searches and drops caches. Durable state is kept. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	void RemoveOwner(const FDocOwnerScope& Owner);

	// ---- Commands ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	FDocSystemResult DiscoverEntry(const FDocOwnerScope& Owner, FName EntryId, const FDocKnowledgeGrant& Grant);

	/** Reveal up to Stage (monotonic: a lower or equal stage is NoChange). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	FDocSystemResult RevealEntry(const FDocOwnerScope& Owner, FName EntryId, int32 Stage, const FDocKnowledgeGrant& Grant);

	/** Reveal a gated section (the handoff's UpdateEntry). Requires discovery and CanUpdate. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	FDocSystemResult UpdateEntry(const FDocOwnerScope& Owner, FName EntryId, FName SectionId, const FDocKnowledgeGrant& Grant);

	/** Acknowledge the revision that was actually displayed; a later unseen update stays unread. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	FDocSystemResult MarkRead(const FDocOwnerScope& Owner, FName EntryId, int64 DisplayedRevision);

	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	FDocSystemResult CompleteEntry(const FDocOwnerScope& Owner, FName EntryId, const FDocKnowledgeGrant& Grant);

	/** Authorized visibility change; discovery history is kept. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	FDocSystemResult ConcealEntry(const FDocOwnerScope& Owner, FName EntryId, bool bHidden);

	/** Authorized revoke to a lower stage. Removed content is not "unread"; completion is cleared only on request. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	FDocSystemResult RevokeReveal(const FDocOwnerScope& Owner, FName EntryId, int32 NewStage, bool bClearCompletion);

	/** Administrative compatibility operation, validated against legal transitions. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	FDocSystemResult SetEntryState(const FDocOwnerScope& Owner, FName EntryId, EDocKnowledgeDisplayLabel Label);

	// ---- Queries (visibility-filtered) ----
	UFUNCTION(BlueprintPure, Category = "Doc|Knowledge")
	bool IsDiscovered(const FDocOwnerScope& Owner, FName EntryId) const;

	/** Combined (player + linked shared) state; read state is the player's own. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	FDocKnowledgeRuntimeState GetEntryState(const FDocOwnerScope& Owner, FName EntryId) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	TArray<FName> GetEntriesByCategory(const FDocOwnerScope& Owner, FGameplayTag Category, bool bIncludeChildren = true) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	TArray<FName> GetEntriesByTag(const FDocOwnerScope& Owner, FGameplayTag Tag) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	bool GetVisibleEntry(const FDocOwnerScope& Owner, FName EntryId, FDocKnowledgeVisibleEntry& OutEntry) const;

	/** Directed, bounded traversal over visible entries and visible links (visited set; cycles are fine). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	TArray<FName> GetRelatedVisibleEntries(const FDocOwnerScope& Owner, FName EntryId, int32 MaxDepth = 1, int32 MaxResults = 32) const;

	/** Synchronous, paginated search over the owner's visible index. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Knowledge")
	FDocKnowledgeSearchPage SearchEntriesNow(const FDocOwnerScope& Owner, const FDocKnowledgeSearchQuery& Query);

	/**
	 * Asynchronous search: matching runs on copied plain text off the game thread; the
	 * result is applied only if the owner's index generation still matches.
	 */
	FDocRequestHandle SearchEntries(const FDocOwnerScope& Owner, const FDocKnowledgeSearchQuery& Query, FDocKnowledgeSearchCallback OnComplete);

	/** Idempotent; the callback receives Cancelled once. */
	void CancelSearch(FDocRequestHandle Handle);

	// ---- Persistence ----
	FDocKnowledgeSaveData CaptureState() const;
	/** Validate -> migrate (redirect/quarantine) -> apply -> rebuild -> one refresh event. No toasts. */
	FDocSystemResult RestoreState(const FDocKnowledgeSaveData& Data);

	// ---- Delegates ----
	UPROPERTY(BlueprintAssignable, Category = "Doc|Knowledge") FDocKnowledgeChangeEvent OnEntryDiscovered;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Knowledge") FDocKnowledgeChangeEvent OnEntryUpdated;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Knowledge") FDocKnowledgeChangeEvent OnEntryRead;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Knowledge") FDocKnowledgeRefreshEvent OnStateRefreshed;
	FDocKnowledgeChangeNative OnEntryDiscoveredNative;
	FDocKnowledgeChangeNative OnEntryUpdatedNative;
	FDocKnowledgeChangeNative OnEntryReadNative;
	FSimpleMulticastDelegate OnStateRefreshedNative;

	// ---- Culture / tests ----
	/** Invalidates culture-dependent search/sort caches (bound to the culture-changed event). */
	void HandleCultureChanged();
	int32 GetIndexBuildCount() const { return IndexBuildCount; }

	using FSearchWork = TFunction<FDocKnowledgeSearchPage()>;
	using FSearchApply = TFunction<void(FDocKnowledgeSearchPage)>;
	using FSearchDispatcher = TFunction<void(FSearchWork, FSearchApply)>;
	/** Tests: control when search work runs and completes. */
	void SetSearchDispatcherForTesting(FSearchDispatcher Dispatcher) { SearchDispatcher = MoveTemp(Dispatcher); }
	static void SetSubsystemOverrideForTesting(UDocKnowledgeSubsystem* Override);
	void SetUtcClockForTesting(TFunction<int64()> Clock) { UtcClock = MoveTemp(Clock); }

#if !UE_BUILD_SHIPPING
	/** Debug: requesting owner, reveal revisions (for cross-player leak checks). Not a player API. */
	FString DebugDescribeOwner(const FDocOwnerScope& Owner) const;
#endif

	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	struct FOwnerData
	{
		TMap<FName, FDocKnowledgeRuntimeState> Entries;
		TSharedPtr<FDocReceiptLedger> Ledger;
		int64 Revision = 0;
	};

	/** Copied, visibility-filtered text for one owner (safe for worker threads). */
	struct FIndexedEntry
	{
		FName EntryId;
		FGameplayTag Category;
		/** Category plus its parents, precomputed on the game thread (worker matching is array-only). */
		FGameplayTagContainer CategoryWithParents;
		FGameplayTagContainer Tags;
		bool bPlaceholder = false;
		FString TitleLower;
		FString SummaryLower;
		FString BodyLower;
		TArray<FName> VisibleRelations;
		int32 SortRank = 0;
	};

	struct FIndexKey
	{
		int64 OwnerRevision = 0;
		int64 SharedRevision = 0;
		int32 Culture = 0;
		int32 Catalog = 0;
		bool operator==(const FIndexKey& O) const { return OwnerRevision == O.OwnerRevision && SharedRevision == O.SharedRevision && Culture == O.Culture && Catalog == O.Catalog; }
	};

	struct FVisibleIndex
	{
		FIndexKey Key;
		TArray<FIndexedEntry> Entries; // sorted by SortRank
	};

	struct FSearchRecord
	{
		FDocOwnerScope Owner;
		FIndexKey Key;
		FDocKnowledgeSearchCallback Callback;
	};

	const UDocKnowledgeSettings* Settings() const;
	int64 NowUtc() const { return UtcClock ? UtcClock() : FDateTime::UtcNow().GetTicks(); }
	FOwnerData& GetOwnerData(const FDocOwnerScope& Owner);
	const FDocKnowledgeRuntimeState* FindState(const FDocOwnerScope& Owner, FName EntryId) const;
	FDocKnowledgeRuntimeState& EditState(const FDocOwnerScope& Owner, FName EntryId);
	FDocKnowledgeRuntimeState Combined(const FDocOwnerScope& Owner, FName EntryId) const;
	bool IsVisible(const FDocKnowledgeRuntimeState& State, const UDocKnowledgeEntry& Entry) const;
	bool IsPlaceholder(const FDocKnowledgeRuntimeState& State, const UDocKnowledgeEntry& Entry) const;
	bool BuildVisibleEntry(const FDocOwnerScope& Owner, const UDocKnowledgeEntry& Entry, FDocKnowledgeVisibleEntry& Out) const;
	TArray<FName> VisibleRelationTargets(const FDocOwnerScope& Owner, const UDocKnowledgeEntry& Entry, const FDocKnowledgeRuntimeState& State) const;
	FDocSystemResult Validate(const FDocOwnerScope& Owner, FName EntryId, const UDocKnowledgeEntry*& OutEntry) const;
	bool CheckGrant(const FDocOwnerScope& Owner, const FDocKnowledgeGrant& Grant, int64 PayloadHash, FDocSystemResult& OutPrior) const;
	void RecordGrant(const FDocOwnerScope& Owner, const FDocKnowledgeGrant& Grant, int64 PayloadHash, const FDocSystemResult& Result);
	void Touch(const FDocOwnerScope& Owner);
	void Notify(FDocKnowledgeChangeEvent& Dynamic, FDocKnowledgeChangeNative& Native, const FDocOwnerScope& Owner, const UDocKnowledgeEntry& Entry, int64 OldRevision, const FString& Cause);

	FIndexKey CurrentKey(const FDocOwnerScope& Owner) const;
	TSharedPtr<const FVisibleIndex> GetIndex(const FDocOwnerScope& Owner);
	static FDocKnowledgeSearchPage RunSearch(const FVisibleIndex& Index, const FDocKnowledgeSearchQuery& Query, int32 MaxPageSize);
	void CompleteSearch(FDocRequestHandle Handle, FDocKnowledgeSearchPage Page);
	void CancelSearchesFor(const FDocOwnerScope& Owner);

	TMap<FName, TObjectPtr<UDocKnowledgeEntry>> Entries;
	UPROPERTY() TArray<TObjectPtr<UDocKnowledgeEntry>> EntryRefs;
	TMap<FName, FName> Redirects;
	TMap<FDocOwnerScope, FOwnerData> Owners;
	TMap<FDocOwnerScope, FDocOwnerScope> SharedLinks;
	TSharedPtr<IDocKnowledgeMediaProvider> MediaProvider;

	TMap<FDocOwnerScope, TSharedPtr<const FVisibleIndex>> IndexCache;
	int32 CultureGeneration = 1;
	int32 CatalogGeneration = 1;
	int32 IndexBuildCount = 0;

	TDocHandleTable<FSearchRecord> Searches;
	FSearchDispatcher SearchDispatcher;
	TFunction<int64()> UtcClock;
	FDelegateHandle CultureChangedHandle;
};
