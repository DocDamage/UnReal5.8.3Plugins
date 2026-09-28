#include "DocModDefinitionProvider.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool UDocModSampleDefinitionProvider::ValidateDefinition(const FDocModDefinitionEntry& Entry, const FString& FileContent, FString& OutError)
{
	if (bSimulateValidateFailure)
	{
		if (SimulateFailureDefinitionId.IsEmpty() || SimulateFailureDefinitionId == Entry.DefinitionId)
		{
			OutError = FString::Printf(TEXT("Simulated validation failure for definition: %s"), *Entry.DefinitionId);
			return false;
		}
	}

	if (FileContent.IsEmpty())
	{
		OutError = TEXT("Definition content is empty.");
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		OutError = TEXT("Definition file is not valid JSON.");
		return false;
	}

	return true;
}

bool UDocModSampleDefinitionProvider::StageDefinition(const FDocModDefinitionEntry& Entry, const FString& FileContent, FString& OutError)
{
	if (bSimulateStageFailure)
	{
		if (SimulateFailureDefinitionId.IsEmpty() || SimulateFailureDefinitionId == Entry.DefinitionId)
		{
			OutError = FString::Printf(TEXT("Simulated staging failure for definition: %s"), *Entry.DefinitionId);
			return false;
		}
	}

	StagedDefinitions.Add(Entry.DefinitionId, FileContent);
	return true;
}

void UDocModSampleDefinitionProvider::CommitStagedDefinitions()
{
	for (const auto& Kvp : StagedDefinitions)
	{
		CommittedDefinitions.Add(Kvp.Key, Kvp.Value);
	}
	StagedDefinitions.Empty();
}

void UDocModSampleDefinitionProvider::RollbackStagedDefinitions()
{
	StagedDefinitions.Empty();
}

void UDocModSampleDefinitionProvider::DeactivateDefinition(const FString& DefinitionId)
{
	CommittedDefinitions.Remove(DefinitionId);
	StagedDefinitions.Remove(DefinitionId);
}

bool UDocModSampleDefinitionProvider::HasCommittedDefinition(const FString& DefinitionId) const
{
	return CommittedDefinitions.Contains(DefinitionId);
}

FString UDocModSampleDefinitionProvider::GetCommittedDefinitionContent(const FString& DefinitionId) const
{
	const FString* Found = CommittedDefinitions.Find(DefinitionId);
	return Found ? *Found : TEXT("");
}
