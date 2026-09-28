#include "DocModContentTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FDocModSemVer::Parse(const FString& InVersionString, FDocModSemVer& OutSemVer)
{
	FString Clean = InVersionString.TrimStartAndEnd();
	if (Clean.StartsWith(TEXT("v")) || Clean.StartsWith(TEXT("V")))
	{
		Clean = Clean.RightChop(1);
	}

	FString MainPart = Clean;
	FString PreReleasePart;
	if (Clean.Split(TEXT("-"), &MainPart, &PreReleasePart))
	{
		OutSemVer.PreRelease = PreReleasePart;
	}
	else
	{
		OutSemVer.PreRelease.Empty();
	}

	TArray<FString> Parts;
	MainPart.ParseIntoArray(Parts, TEXT("."));
	if (Parts.Num() < 1 || Parts.Num() > 3)
	{
		return false;
	}

	if (!Parts[0].IsNumeric())
	{
		return false;
	}
	OutSemVer.Major = FCString::Atoi(*Parts[0]);

	if (Parts.Num() >= 2)
	{
		if (!Parts[1].IsNumeric())
		{
			return false;
		}
		OutSemVer.Minor = FCString::Atoi(*Parts[1]);
	}
	else
	{
		OutSemVer.Minor = 0;
	}

	if (Parts.Num() >= 3)
	{
		if (!Parts[2].IsNumeric())
		{
			return false;
		}
		OutSemVer.Patch = FCString::Atoi(*Parts[2]);
	}
	else
	{
		OutSemVer.Patch = 0;
	}

	return OutSemVer.Major >= 0 && OutSemVer.Minor >= 0 && OutSemVer.Patch >= 0;
}

FString FDocModSemVer::ToString() const
{
	if (PreRelease.IsEmpty())
	{
		return FString::Printf(TEXT("%d.%d.%d"), Major, Minor, Patch);
	}
	return FString::Printf(TEXT("%d.%d.%d-%s"), Major, Minor, Patch, *PreRelease);
}

bool FDocModSemVer::operator<(const FDocModSemVer& Other) const
{
	if (Major != Other.Major) return Major < Other.Major;
	if (Minor != Other.Minor) return Minor < Other.Minor;
	return Patch < Other.Patch;
}

bool FDocModSemVer::operator<=(const FDocModSemVer& Other) const
{
	return (*this < Other) || (*this == Other);
}

bool FDocModSemVer::operator>(const FDocModSemVer& Other) const
{
	return !(*this <= Other);
}

bool FDocModSemVer::operator>=(const FDocModSemVer& Other) const
{
	return !(*this < Other);
}

bool FDocModSemVer::operator==(const FDocModSemVer& Other) const
{
	return Major == Other.Major && Minor == Other.Minor && Patch == Other.Patch && PreRelease == Other.PreRelease;
}

bool FDocModSemVer::operator!=(const FDocModSemVer& Other) const
{
	return !(*this == Other);
}

bool FDocModSemVer::MatchesConstraint(const FString& Constraint) const
{
	FString Clean = Constraint.TrimStartAndEnd();
	if (Clean.IsEmpty() || Clean == TEXT("*"))
	{
		return true;
	}

	if (Clean.StartsWith(TEXT(">=")))
	{
		FDocModSemVer Target;
		if (FDocModSemVer::Parse(Clean.RightChop(2), Target))
		{
			return *this >= Target;
		}
		return false;
	}
	if (Clean.StartsWith(TEXT("<=")))
	{
		FDocModSemVer Target;
		if (FDocModSemVer::Parse(Clean.RightChop(2), Target))
		{
			return *this <= Target;
		}
		return false;
	}
	if (Clean.StartsWith(TEXT(">")))
	{
		FDocModSemVer Target;
		if (FDocModSemVer::Parse(Clean.RightChop(1), Target))
		{
			return *this > Target;
		}
		return false;
	}
	if (Clean.StartsWith(TEXT("<")))
	{
		FDocModSemVer Target;
		if (FDocModSemVer::Parse(Clean.RightChop(1), Target))
		{
			return *this < Target;
		}
		return false;
	}
	if (Clean.StartsWith(TEXT("^")))
	{
		// Caret requirement: compatible within same major version (e.g. ^1.2.3 -> >=1.2.3 and <2.0.0)
		FDocModSemVer Target;
		if (FDocModSemVer::Parse(Clean.RightChop(1), Target))
		{
			if (*this < Target) return false;
			return Major == Target.Major;
		}
		return false;
	}
	if (Clean.StartsWith(TEXT("==")))
	{
		Clean = Clean.RightChop(2);
	}

	FDocModSemVer Target;
	if (FDocModSemVer::Parse(Clean, Target))
	{
		return *this == Target;
	}

	return false;
}

bool FDocModManifest::ParseFromJson(const FString& JsonContent, FDocModManifest& OutManifest, FString& OutError)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		OutError = TEXT("Failed to parse JSON content or invalid root object.");
		return false;
	}

	if (!JsonObject->HasTypedField<EJson::Number>(TEXT("manifest_version")))
	{
		OutError = TEXT("Missing or invalid 'manifest_version'.");
		return false;
	}
	OutManifest.ManifestVersion = JsonObject->GetIntegerField(TEXT("manifest_version"));
	if (OutManifest.ManifestVersion <= 0)
	{
		OutError = TEXT("manifest_version must be positive.");
		return false;
	}

	if (!JsonObject->HasTypedField<EJson::String>(TEXT("pack_id")))
	{
		OutError = TEXT("Missing 'pack_id'.");
		return false;
	}
	OutManifest.PackId = JsonObject->GetStringField(TEXT("pack_id")).TrimStartAndEnd();
	if (OutManifest.PackId.IsEmpty())
	{
		OutError = TEXT("'pack_id' cannot be empty.");
		return false;
	}

	FString VersionStr = JsonObject->GetStringField(TEXT("pack_version"));
	if (!FDocModSemVer::Parse(VersionStr, OutManifest.PackVersion))
	{
		OutError = FString::Printf(TEXT("Invalid semver format for 'pack_version': %s"), *VersionStr);
		return false;
	}

	OutManifest.DisplayName = JsonObject->GetStringField(TEXT("display_name"));

	// Requires array
	OutManifest.Requires.Empty();
	const TArray<TSharedPtr<FJsonValue>>* RequiresJsonArray = nullptr;
	if (JsonObject->TryGetArrayField(TEXT("requires"), RequiresJsonArray) && RequiresJsonArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *RequiresJsonArray)
		{
			if (Val->Type == EJson::String)
			{
				FDocModDependency Dep;
				Dep.PackId = Val->AsString();
				Dep.VersionConstraint = TEXT("*");
				OutManifest.Requires.Add(Dep);
			}
			else if (Val->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> DepObj = Val->AsObject();
				if (DepObj.IsValid())
				{
					FDocModDependency Dep;
					Dep.PackId = DepObj->GetStringField(TEXT("pack_id"));
					Dep.VersionConstraint = DepObj->HasField(TEXT("version")) ? DepObj->GetStringField(TEXT("version")) : TEXT("*");
					Dep.bOptional = DepObj->HasField(TEXT("optional")) && DepObj->GetBoolField(TEXT("optional"));
					if (!Dep.PackId.IsEmpty())
					{
						OutManifest.Requires.Add(Dep);
					}
				}
			}
		}
	}

	// Conflicts array
	OutManifest.Conflicts.Empty();
	const TArray<TSharedPtr<FJsonValue>>* ConflictsJsonArray = nullptr;
	if (JsonObject->TryGetArrayField(TEXT("conflicts"), ConflictsJsonArray) && ConflictsJsonArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *ConflictsJsonArray)
		{
			if (Val->Type == EJson::String && !Val->AsString().IsEmpty())
			{
				OutManifest.Conflicts.Add(Val->AsString());
			}
		}
	}

	// Definitions array
	OutManifest.Definitions.Empty();
	const TArray<TSharedPtr<FJsonValue>>* DefsJsonArray = nullptr;
	if (JsonObject->TryGetArrayField(TEXT("definitions"), DefsJsonArray) && DefsJsonArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *DefsJsonArray)
		{
			if (Val->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> DefObj = Val->AsObject();
				if (DefObj.IsValid())
				{
					FDocModDefinitionEntry Entry;
					Entry.DefinitionId = DefObj->GetStringField(TEXT("id"));
					Entry.Schema = DefObj->GetStringField(TEXT("schema"));
					Entry.SchemaVersion = DefObj->HasField(TEXT("schema_version")) ? DefObj->GetIntegerField(TEXT("schema_version")) : 1;
					Entry.RelativePath = DefObj->GetStringField(TEXT("path"));

					if (Entry.DefinitionId.IsEmpty() || Entry.Schema.IsEmpty() || Entry.RelativePath.IsEmpty())
					{
						OutError = TEXT("Definition entries must contain non-empty 'id', 'schema', and 'path'.");
						return false;
					}

					OutManifest.Definitions.Add(Entry);
				}
			}
		}
	}

	return true;
}

FString FDocModManifest::ToJson() const
{
	TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();
	RootObj->SetNumberField(TEXT("manifest_version"), ManifestVersion);
	RootObj->SetStringField(TEXT("pack_id"), PackId);
	RootObj->SetStringField(TEXT("pack_version"), PackVersion.ToString());
	RootObj->SetStringField(TEXT("display_name"), DisplayName);

	// Requires
	TArray<TSharedPtr<FJsonValue>> ReqArray;
	for (const FDocModDependency& Dep : Requires)
	{
		TSharedPtr<FJsonObject> DepObj = MakeShared<FJsonObject>();
		DepObj->SetStringField(TEXT("pack_id"), Dep.PackId);
		DepObj->SetStringField(TEXT("version"), Dep.VersionConstraint);
		if (Dep.bOptional)
		{
			DepObj->SetBoolField(TEXT("optional"), true);
		}
		ReqArray.Add(MakeShared<FJsonValueObject>(DepObj));
	}
	RootObj->SetArrayField(TEXT("requires"), ReqArray);

	// Conflicts
	TArray<TSharedPtr<FJsonValue>> ConfArray;
	for (const FString& Conf : Conflicts)
	{
		ConfArray.Add(MakeShared<FJsonValueString>(Conf));
	}
	RootObj->SetArrayField(TEXT("conflicts"), ConfArray);

	// Definitions
	TArray<TSharedPtr<FJsonValue>> DefsArray;
	for (const FDocModDefinitionEntry& Def : Definitions)
	{
		TSharedPtr<FJsonObject> DefObj = MakeShared<FJsonObject>();
		DefObj->SetStringField(TEXT("id"), Def.DefinitionId);
		DefObj->SetStringField(TEXT("schema"), Def.Schema);
		DefObj->SetNumberField(TEXT("schema_version"), Def.SchemaVersion);
		DefObj->SetStringField(TEXT("path"), Def.RelativePath);
		DefsArray.Add(MakeShared<FJsonValueObject>(DefObj));
	}
	RootObj->SetArrayField(TEXT("definitions"), DefsArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);
	return OutputString;
}
