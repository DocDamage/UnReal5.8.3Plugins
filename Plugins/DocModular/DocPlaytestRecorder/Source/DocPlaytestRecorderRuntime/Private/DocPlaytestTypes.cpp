#include "DocPlaytestTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString FDocPlaytestExportManifest::ToJson() const
{
	TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();
	RootObj->SetStringField(TEXT("session_id"), SessionId);
	RootObj->SetStringField(TEXT("issue_id"), IssueId.ToString());
	RootObj->SetStringField(TEXT("tester_note"), TesterNote);
	RootObj->SetStringField(TEXT("start_time"), StartTime.ToIso8601());
	RootObj->SetStringField(TEXT("end_time"), EndTime.ToIso8601());
	RootObj->SetNumberField(TEXT("total_events"), TotalEvents);
	RootObj->SetNumberField(TEXT("evicted_events"), EvictedEvents);

	// Files
	TSharedPtr<FJsonObject> FilesObj = MakeShared<FJsonObject>();
	for (const auto& Kvp : ExportedFiles)
	{
		FilesObj->SetNumberField(Kvp.Key, Kvp.Value);
	}
	RootObj->SetObjectField(TEXT("files"), FilesObj);

	// Hashes
	TSharedPtr<FJsonObject> HashesObj = MakeShared<FJsonObject>();
	for (const auto& Kvp : FileHashes)
	{
		HashesObj->SetStringField(Kvp.Key, Kvp.Value);
	}
	RootObj->SetObjectField(TEXT("hashes"), HashesObj);

	// Omissions
	TArray<TSharedPtr<FJsonValue>> OmissionsArray;
	for (const FDocDiagnosticOmission& Om : Omissions)
	{
		TSharedPtr<FJsonObject> OmObj = MakeShared<FJsonObject>();
		OmObj->SetStringField(TEXT("provider"), Om.ProviderName);
		OmObj->SetStringField(TEXT("reason"), Om.Reason);
		OmObj->SetStringField(TEXT("timestamp"), Om.Timestamp.ToIso8601());
		OmissionsArray.Add(MakeShared<FJsonValueObject>(OmObj));
	}
	RootObj->SetArrayField(TEXT("omissions"), OmissionsArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);
	return OutputString;
}

bool FDocPlaytestExportManifest::ParseFromJson(const FString& JsonContent, FDocPlaytestExportManifest& OutManifest, FString& OutError)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		OutError = TEXT("Failed to parse manifest JSON.");
		return false;
	}

	OutManifest.SessionId = JsonObject->GetStringField(TEXT("session_id"));
	FGuid::Parse(JsonObject->GetStringField(TEXT("issue_id")), OutManifest.IssueId);
	OutManifest.TesterNote = JsonObject->GetStringField(TEXT("tester_note"));
	FDateTime::ParseIso8601(*JsonObject->GetStringField(TEXT("start_time")), OutManifest.StartTime);
	FDateTime::ParseIso8601(*JsonObject->GetStringField(TEXT("end_time")), OutManifest.EndTime);
	OutManifest.TotalEvents = JsonObject->GetIntegerField(TEXT("total_events"));
	OutManifest.EvictedEvents = JsonObject->GetIntegerField(TEXT("evicted_events"));

	// Files
	OutManifest.ExportedFiles.Empty();
	const TSharedPtr<FJsonObject>* FilesObj = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("files"), FilesObj) && FilesObj && FilesObj->IsValid())
	{
		for (const auto& Kvp : (*FilesObj)->Values)
		{
			OutManifest.ExportedFiles.Add(FString(Kvp.Key), (int64)Kvp.Value->AsNumber());
		}
	}

	// Hashes
	OutManifest.FileHashes.Empty();
	const TSharedPtr<FJsonObject>* HashesObj = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("hashes"), HashesObj) && HashesObj && HashesObj->IsValid())
	{
		for (const auto& Kvp : (*HashesObj)->Values)
		{
			OutManifest.FileHashes.Add(FString(Kvp.Key), Kvp.Value->AsString());
		}
	}


	// Omissions
	OutManifest.Omissions.Empty();
	const TArray<TSharedPtr<FJsonValue>>* OmissionsArray = nullptr;
	if (JsonObject->TryGetArrayField(TEXT("omissions"), OmissionsArray) && OmissionsArray)
	{
		for (const auto& Val : *OmissionsArray)
		{
			TSharedPtr<FJsonObject> OmObj = Val->AsObject();
			if (OmObj.IsValid())
			{
				FDocDiagnosticOmission Om;
				Om.ProviderName = OmObj->GetStringField(TEXT("provider"));
				Om.Reason = OmObj->GetStringField(TEXT("reason"));
				FDateTime::ParseIso8601(*OmObj->GetStringField(TEXT("timestamp")), Om.Timestamp);
				OutManifest.Omissions.Add(Om);
			}
		}
	}

	return true;
}
