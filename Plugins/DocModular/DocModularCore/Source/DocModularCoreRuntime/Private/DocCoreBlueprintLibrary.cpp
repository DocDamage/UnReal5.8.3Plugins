#include "DocCoreBlueprintLibrary.h"
#include "Interfaces/DocPersistentIdentity.h"
#include "Interfaces/DocGameplayTagProvider.h"
#include "GameplayTagAssetInterface.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocCoreBlueprintLibrary)

FDocSystemResult UDocCoreBlueprintLibrary::MakeFailureResult(EDocResultOutcome Outcome, const FString& Diagnostic, FGameplayTag ErrorTag, FText UserMessage)
{
	if (FDocSystemResult::IsSuccessOutcome(Outcome) || Outcome == EDocResultOutcome::Unset)
	{
		// Blueprint authors can pick any enum value; do not ensure() on user input.
		Outcome = EDocResultOutcome::Failed;
	}
	return FDocSystemResult::MakeFailure(Outcome, Diagnostic, ErrorTag, UserMessage);
}

FDocGameplayContext UDocCoreBlueprintLibrary::MakeDocGameplayContext(const UObject* WorldContextObject, AActor* Instigator, AActor* Target)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return FDocGameplayContext::Make(World, Instigator, Target);
}

bool UDocCoreBlueprintLibrary::ParsePersistentId(const FString& Text, FDocPersistentObjectId& OutId)
{
	OutId = FDocPersistentObjectId();
	return FDocPersistentObjectId::Parse(Text, OutId);
}

FDocPersistentObjectId UDocCoreBlueprintLibrary::MakeRuntimePersistentId(const FGuid& WorldNamespace, const FGuid& InstanceScope)
{
	return FDocPersistentObjectId(WorldNamespace, InstanceScope, FGuid::NewGuid());
}

FDocPersistentObjectId UDocCoreBlueprintLibrary::GetPersistentIdFromObject(const UObject* Object)
{
	if (Object && Object->Implements<UDocPersistentIdentity>())
	{
		return IDocPersistentIdentity::Execute_GetDocPersistentId(Object);
	}
	return FDocPersistentObjectId();
}

bool UDocCoreBlueprintLibrary::GetOwnedTagsFromObject(const UObject* Object, FGameplayTagContainer& OutTags)
{
	OutTags.Reset();
	if (!Object)
	{
		return false;
	}

	if (Object->Implements<UDocGameplayTagProvider>())
	{
		IDocGameplayTagProvider::Execute_GetDocOwnedTags(Object, OutTags);
		return true;
	}

	if (const IGameplayTagAssetInterface* TagAsset = Cast<IGameplayTagAssetInterface>(Object))
	{
		TagAsset->GetOwnedGameplayTags(OutTags);
		return true;
	}
	return false;
}
