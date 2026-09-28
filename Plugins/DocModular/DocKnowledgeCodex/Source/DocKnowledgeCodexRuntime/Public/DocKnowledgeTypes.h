#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "UObject/SoftObjectPath.h"
#include "DocEffectKey.h"
#include "DocOwnerScope.h"
#include "DocSystemResult.h"
#include "DocKnowledgeTypes.generated.h"

/**
 * DocKnowledgeCodex data model (Modules 11-20 handoff, Section 9; Module 17).
 *
 * Name mapping to the handoff's semantic contracts:
 *   KnowledgeSubsystem     -> UDocKnowledgeSubsystem (GameInstance)
 *   KnowledgeEntry         -> UDocKnowledgeEntry
 *   KnowledgeCategory      -> Knowledge.* gameplay tags (data, not a fixed taxonomy)
 *   KnowledgeRuntimeState  -> FDocKnowledgeRuntimeState
 */
namespace DocKnowledgeTags
{
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Knowledge);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Knowledge_Tutorial);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Knowledge_Location);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Knowledge_Character);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Knowledge_Creature);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Knowledge_Item);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Knowledge_History);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Knowledge_Clue);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Knowledge_Document);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Knowledge_Mechanic);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Knowledge_Custom);

	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Knowledge);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Knowledge_UnknownEntry);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Knowledge_NotDiscovered);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Knowledge_CannotUpdate);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Knowledge_StaleRevision);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Knowledge_IllegalTransition);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Knowledge_Quarantined);
	DOCKNOWLEDGECODEXRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Knowledge_SearchStale);
}

UENUM(BlueprintType)
enum class EDocKnowledgeRelationType : uint8
{
	Character,
	Location,
	Item,
	Event,
	Document,
	Custom
};

UENUM(BlueprintType)
enum class EDocKnowledgeMediaType : uint8
{
	Image,
	Audio,
	Video,
	/** External source; only a configured media provider may resolve it. Never executed. */
	External
};

/** Compatibility display labels, derived from the separate state dimensions. */
UENUM(BlueprintType)
enum class EDocKnowledgeDisplayLabel : uint8
{
	Unknown,
	Discovered,
	/** New visible content exists beyond the last read revision. */
	Updated,
	Read,
	Completed,
	Hidden
};

UENUM(BlueprintType)
enum class EDocKnowledgeUpdateCountPolicy : uint8
{
	/** Stage reveals after the first discovery and section updates both count. */
	RevealsAndSections,
	SectionsOnly
};

USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeSection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FName SectionId;

	/** Visible once the entry reaches this stage. 0 = gated: only UpdateEntry(SectionId) reveals it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge", meta = (ClampMin = "0"))
	int32 RevealStage = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FText Heading;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge", meta = (MultiLine = true))
	FText Body;

	/** Bump when the section's text changes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge", meta = (ClampMin = "1"))
	int32 Revision = 1;
};

USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeMedia
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FName MediaId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	EDocKnowledgeMediaType Type = EDocKnowledgeMediaType::Image;

	/** Approved soft reference (image/sound/media source). Never loaded by the base. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FSoftObjectPath Asset;

	/** External media only: resolved exclusively by a configured provider. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FString ExternalUrl;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FText Caption;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge", meta = (MultiLine = true))
	FText Transcript;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge", meta = (ClampMin = "1"))
	int32 RevealStage = 3;
};

/** Directed relationship (not automatically bidirectional). */
USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeRelation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	EDocKnowledgeRelationType Type = EDocKnowledgeRelationType::Character;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FName TargetEntryId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge", meta = (EditCondition = "Type == EDocKnowledgeRelationType::Custom"))
	FGameplayTag CustomType;

	/** Source entry stage required before this link is shown. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge", meta = (ClampMin = "1"))
	int32 RevealStage = 1;
};

/** KnowledgeEntry. Immutable at runtime. Stage 0 Unknown / 1 Name / 2 Summary / 3 Full is the default example. */
UCLASS(BlueprintType)
class DOCKNOWLEDGECODEXRUNTIME_API UDocKnowledgeEntry : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FName EntryId;

	/** Knowledge.* (or a project child). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge", meta = (Categories = "Knowledge"))
	FGameplayTag Category;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FText Summary;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge", meta = (MultiLine = true))
	FText Body;

	/** Images, Audio and Video (and external sources) as optional descriptors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	TArray<FDocKnowledgeMedia> Media;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FGameplayTagContainer Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	TArray<FDocKnowledgeRelation> Relations;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	int32 SortOrder = 0;

	/** False: listed as a placeholder (no text) before discovery. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	bool bHiddenUntilDiscovered = true;

	/** False: UpdateEntry (section updates) is refused after discovery. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	bool bCanUpdate = true;

	/** VersionedSections. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	TArray<FDocKnowledgeSection> Sections;

	/** Content revision of the whole entry (bump on edits). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge", meta = (ClampMin = "1"))
	int32 ContentRevision = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stages", meta = (ClampMin = "1"))
	int32 MaxStage = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stages", meta = (ClampMin = "1"))
	int32 TitleStage = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stages", meta = (ClampMin = "1"))
	int32 SummaryStage = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stages", meta = (ClampMin = "1"))
	int32 BodyStage = 3;

	/** Editable example labels (Unknown, Name, Summary, Full). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stages")
	TArray<FText> StageLabels;

	/** Reaching MaxStage also marks the entry completed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stages")
	bool bCompleteAtMaxStage = false;

	const FDocKnowledgeSection* FindSection(FName SectionId) const
	{
		return Sections.FindByPredicate([SectionId](const FDocKnowledgeSection& S) { return S.SectionId == SectionId; });
	}

	void FindProblems(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("DocKnowledgeEntry"), GetFName()); }
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

/** Catalog of entries plus removal redirects. */
UCLASS(BlueprintType)
class DOCKNOWLEDGECODEXRUNTIME_API UDocKnowledgeCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	TArray<TObjectPtr<UDocKnowledgeEntry>> Entries;

	/** Removed/merged EntryId -> replacement, applied on restore. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	TMap<FName, FName> EntryRedirects;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("DocKnowledgeCatalog"), GetFName()); }
};

// ---------------------------------------------------------------------------
// Runtime state (also the save schema)
// ---------------------------------------------------------------------------

/** KnowledgeRuntimeState: separate dimensions, not one overlapping enum. */
USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	FName EntryId;

	/** Discovery/reveal stage (0 = unknown). Monotonic unless an explicit revoke. */
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	int32 Stage = 0;

	/** Visibility: concealing never erases discovery history. */
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	bool bHidden = false;

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	bool bCompleted = false;

	/** Monotonic counter of visible content changes. */
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	int64 VisibleRevision = 0;

	/** Last revision the player acknowledged (displayed revision, never "latest"). */
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	int64 ReadRevision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	int32 UpdateCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	int64 DiscoveryTimeUtcTicks = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	int64 LastUpdateTimeUtcTicks = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	int64 ReadTimeUtcTicks = 0;

	/** Gated sections revealed through UpdateEntry. */
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	TArray<FName> RevealedSections;

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	int32 ContentRevisionSeen = 0;

	/** Definition removed: kept (never inferred complete), hidden from player APIs, preserved in saves. */
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge")
	bool bQuarantined = false;

	bool IsDiscovered() const { return Stage > 0; }

	EDocKnowledgeDisplayLabel GetDisplayLabel() const
	{
		if (bHidden || bQuarantined) { return EDocKnowledgeDisplayLabel::Hidden; }
		if (Stage <= 0) { return EDocKnowledgeDisplayLabel::Unknown; }
		if (bCompleted) { return EDocKnowledgeDisplayLabel::Completed; }
		if (ReadRevision <= 0) { return EDocKnowledgeDisplayLabel::Discovered; }
		return VisibleRevision > ReadRevision ? EDocKnowledgeDisplayLabel::Updated : EDocKnowledgeDisplayLabel::Read;
	}
};

/** Discovery intent from a source (quest reward, dialogue, inspection bridge...). */
USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeGrant
{
	GENERATED_BODY()

	/** Optional idempotency key: a repeat returns the original result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	FDocEffectKey EffectKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	FName SourceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	FString Cause;
};

/** Change notification. Carries only a visibility-safe title. */
USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeChange
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FDocOwnerScope Owner;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FName EntryId;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") int64 OldRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") int64 NewRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") int32 Stage = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FString Cause;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FText VisibleTitle;
};

USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeSectionView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FName SectionId;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FText Heading;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FText Body;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") int32 Revision = 1;
};

USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeMediaView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FName MediaId;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") EDocKnowledgeMediaType Type = EDocKnowledgeMediaType::Image;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FSoftObjectPath Asset;
	/** False: missing asset, unconfigured external source, or provider refusal. Text fallback applies. */
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") bool bAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FText Caption;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FText Transcript;
	/** Always usable: transcript, caption, or the entry title. */
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FText FallbackText;
};

USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeRelationView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") EDocKnowledgeRelationType Type = EDocKnowledgeRelationType::Character;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FGameplayTag CustomType;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FName TargetEntryId;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FText TargetTitle;
};

/** Visibility-filtered entry for one owner (ordinary player API). */
USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeVisibleEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FName EntryId;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FGameplayTag Category;
	/** Listed before discovery (bHiddenUntilDiscovered = false): no text, tags or links. */
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") bool bPlaceholder = false;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") int32 Stage = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FText StageLabel;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FText Title;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FText Summary;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FText Body;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") TArray<FDocKnowledgeSectionView> Sections;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") TArray<FDocKnowledgeMediaView> Media;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") TArray<FDocKnowledgeRelationView> Relations;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FGameplayTagContainer Tags;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") EDocKnowledgeDisplayLabel Label = EDocKnowledgeDisplayLabel::Unknown;
	/** Submit this to MarkRead after displaying. */
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") int64 VisibleRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") int64 ReadRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") int32 SortOrder = 0;
};

USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeSearchQuery
{
	GENERATED_BODY()

	/** Case-insensitive substring over visible text (title, summary, body, sections as revealed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge") FString Text;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge") bool bSearchBody = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge") FGameplayTag Category;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge") bool bIncludeChildCategories = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge") FGameplayTagContainer RequiredTags;
	/** Only entries with a visible relation to this entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge") FName RelatedTo;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge") int32 Offset = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge") int32 PageSize = 25;
};

USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeSearchPage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") TArray<FName> EntryIds;
	/** Count of visible matches only. */
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") int32 TotalMatches = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") int32 Offset = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") bool bHasMore = false;
};

USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeOwnerSave
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FDocOwnerScope Owner;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") TArray<FDocKnowledgeRuntimeState> Entries;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") TArray<FDocEffectReceipt> Receipts;
};

/** A player scope that also sees a shared scope's discoveries (reads stay per player). */
USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeLink
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FDocOwnerScope Player;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") FDocOwnerScope Shared;
};

USTRUCT(BlueprintType)
struct DOCKNOWLEDGECODEXRUNTIME_API FDocKnowledgeSaveData
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") int32 SchemaVersion = CurrentSchemaVersion;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") TArray<FDocKnowledgeOwnerSave> Owners;
	UPROPERTY(BlueprintReadOnly, Category = "Knowledge") TArray<FDocKnowledgeLink> Links;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Knowledge Codex"))
class DOCKNOWLEDGECODEXRUNTIME_API UDocKnowledgeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	UPROPERTY(Config, EditAnywhere, Category = "State")
	EDocKnowledgeUpdateCountPolicy UpdateCountPolicy = EDocKnowledgeUpdateCountPolicy::RevealsAndSections;

	/** On restore, a newer definition content revision marks discovered entries unread (no event, no update count). */
	UPROPERTY(Config, EditAnywhere, Category = "State")
	bool bFlagRevisedContentAsUpdated = true;

	UPROPERTY(Config, EditAnywhere, Category = "Search", meta = (ClampMin = "1"))
	int32 MaxPageSize = 100;

	UPROPERTY(Config, EditAnywhere, Category = "Relations", meta = (ClampMin = "1"))
	int32 MaxRelationDepth = 4;

	UPROPERTY(Config, EditAnywhere, Category = "Relations", meta = (ClampMin = "1"))
	int32 MaxRelationResults = 64;

	UPROPERTY(Config, EditAnywhere, Category = "Persistence", meta = (ClampMin = "0"))
	int32 MaxReceiptsPerOwner = 1024;
};
