#include "DocKnowledgeTypes.h"
#include "DocKnowledgeCodexLog.h"
#include "Misc/DataValidation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocKnowledgeTypes)

namespace DocKnowledgeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Knowledge, "Knowledge", "DocKnowledgeCodex categories (data, not a fixed taxonomy)");
	UE_DEFINE_GAMEPLAY_TAG(Knowledge_Tutorial, "Knowledge.Tutorial");
	UE_DEFINE_GAMEPLAY_TAG(Knowledge_Location, "Knowledge.Location");
	UE_DEFINE_GAMEPLAY_TAG(Knowledge_Character, "Knowledge.Character");
	UE_DEFINE_GAMEPLAY_TAG(Knowledge_Creature, "Knowledge.Creature");
	UE_DEFINE_GAMEPLAY_TAG(Knowledge_Item, "Knowledge.Item");
	UE_DEFINE_GAMEPLAY_TAG(Knowledge_History, "Knowledge.History");
	UE_DEFINE_GAMEPLAY_TAG(Knowledge_Clue, "Knowledge.Clue");
	UE_DEFINE_GAMEPLAY_TAG(Knowledge_Document, "Knowledge.Document");
	UE_DEFINE_GAMEPLAY_TAG(Knowledge_Mechanic, "Knowledge.Mechanic");
	UE_DEFINE_GAMEPLAY_TAG(Knowledge_Custom, "Knowledge.Custom");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Knowledge, "Doc.Error.Knowledge", "DocKnowledgeCodex errors");
	UE_DEFINE_GAMEPLAY_TAG(Error_Knowledge_UnknownEntry, "Doc.Error.Knowledge.UnknownEntry");
	UE_DEFINE_GAMEPLAY_TAG(Error_Knowledge_NotDiscovered, "Doc.Error.Knowledge.NotDiscovered");
	UE_DEFINE_GAMEPLAY_TAG(Error_Knowledge_CannotUpdate, "Doc.Error.Knowledge.CannotUpdate");
	UE_DEFINE_GAMEPLAY_TAG(Error_Knowledge_StaleRevision, "Doc.Error.Knowledge.StaleRevision");
	UE_DEFINE_GAMEPLAY_TAG(Error_Knowledge_IllegalTransition, "Doc.Error.Knowledge.IllegalTransition");
	UE_DEFINE_GAMEPLAY_TAG(Error_Knowledge_Quarantined, "Doc.Error.Knowledge.Quarantined");
	UE_DEFINE_GAMEPLAY_TAG(Error_Knowledge_SearchStale, "Doc.Error.Knowledge.SearchStale");
}

void UDocKnowledgeEntry::FindProblems(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const
{
	const FString Where = FString::Printf(TEXT("Entry %s"), *EntryId.ToString());
	if (EntryId.IsNone())
	{
		OutErrors.Add(TEXT("EntryId is required"));
	}
	if (!Category.IsValid())
	{
		OutWarnings.Add(Where + TEXT(": no category"));
	}
	if (!(TitleStage <= SummaryStage && SummaryStage <= BodyStage && BodyStage <= MaxStage))
	{
		OutErrors.Add(Where + TEXT(": stages must satisfy Title <= Summary <= Body <= MaxStage"));
	}
	TSet<FName> SectionIds;
	for (const FDocKnowledgeSection& S : Sections)
	{
		bool bDup = false;
		SectionIds.Add(S.SectionId, &bDup);
		if (S.SectionId.IsNone() || bDup) { OutErrors.Add(FString::Printf(TEXT("%s: SectionId missing or duplicated (%s)"), *Where, *S.SectionId.ToString())); }
		if (S.RevealStage > MaxStage) { OutErrors.Add(FString::Printf(TEXT("%s: section %s reveals beyond MaxStage"), *Where, *S.SectionId.ToString())); }
		if (S.RevealStage == 0 && !bCanUpdate) { OutWarnings.Add(FString::Printf(TEXT("%s: gated section %s can never be revealed (CanUpdate is false)"), *Where, *S.SectionId.ToString())); }
	}
	TSet<FName> MediaIds;
	for (const FDocKnowledgeMedia& M : Media)
	{
		bool bDup = false;
		MediaIds.Add(M.MediaId, &bDup);
		if (M.MediaId.IsNone() || bDup) { OutErrors.Add(FString::Printf(TEXT("%s: MediaId missing or duplicated (%s)"), *Where, *M.MediaId.ToString())); }
		if (M.Type == EDocKnowledgeMediaType::External && M.ExternalUrl.IsEmpty()) { OutErrors.Add(FString::Printf(TEXT("%s: external media %s without a source"), *Where, *M.MediaId.ToString())); }
		if (M.Type != EDocKnowledgeMediaType::External && !M.Asset.IsValid()) { OutWarnings.Add(FString::Printf(TEXT("%s: media %s has no asset (text fallback only)"), *Where, *M.MediaId.ToString())); }
		if (M.Caption.IsEmpty() && M.Transcript.IsEmpty() && M.Type != EDocKnowledgeMediaType::Image) { OutWarnings.Add(FString::Printf(TEXT("%s: media %s has no caption or transcript"), *Where, *M.MediaId.ToString())); }
	}
	for (const FDocKnowledgeRelation& R : Relations)
	{
		if (R.TargetEntryId.IsNone()) { OutErrors.Add(Where + TEXT(": relation without target")); }
		if (R.Type == EDocKnowledgeRelationType::Custom && !R.CustomType.IsValid()) { OutErrors.Add(Where + TEXT(": custom relation without CustomType")); }
	}
	if (StageLabels.Num() > MaxStage + 1)
	{
		OutWarnings.Add(Where + TEXT(": more stage labels than stages"));
	}
}

#if WITH_EDITOR
EDataValidationResult UDocKnowledgeEntry::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TArray<FString> Errors, Warnings;
	FindProblems(Errors, Warnings);
	for (const FString& E : Errors)
	{
		Context.AddError(FText::FromString(E));
		Result = EDataValidationResult::Invalid;
	}
	for (const FString& W : Warnings)
	{
		Context.AddWarning(FText::FromString(W));
	}
	return Result;
}
#endif
