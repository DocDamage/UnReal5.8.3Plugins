#include "DocModContentSubsystem.h"
#include "DocModContentSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"

static TWeakObjectPtr<UDocModContentSubsystem> GDocModTestOverride;

UDocModContentSubsystem::UDocModContentSubsystem()
{
	CurrentCatalog.RevisionNumber = 1;
	CurrentCatalog.Timestamp = FDateTime::UtcNow();
}

UDocModContentSubsystem* UDocModContentSubsystem::Get(const UObject* WorldContextObject)
{
	if (UDocModContentSubsystem* Override = GDocModTestOverride.Get())
	{
		return Override;
	}

	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GI = World->GetGameInstance();
	return GI ? GI->GetSubsystem<UDocModContentSubsystem>() : nullptr;
}

void UDocModContentSubsystem::SetSubsystemOverrideForTesting(UDocModContentSubsystem* Override)
{
	GDocModTestOverride = Override;
}

void UDocModContentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDocModContentSubsystem::Deinitialize()
{
	SharedProviders.Empty();
	ObjectProviders.Empty();
	ActivePins.Empty();
	ActiveManifests.Empty();
	QuarantineStore.Empty();

	Super::Deinitialize();
}

void UDocModContentSubsystem::RegisterProvider(TSharedPtr<IDocModDefinitionProvider> Provider)
{
	if (Provider.IsValid())
	{
		SharedProviders.Add(Provider->GetSupportedSchema(), Provider);
	}
}

void UDocModContentSubsystem::RegisterProviderObject(UObject* ProviderObject)
{
	if (ProviderObject)
	{
		if (IDocModDefinitionProvider* Provider = Cast<IDocModDefinitionProvider>(ProviderObject))
		{
			ObjectProviders.Add(Provider->GetSupportedSchema(), ProviderObject);
		}
	}
}

void UDocModContentSubsystem::UnregisterProvider(const FString& Schema)
{
	SharedProviders.Remove(Schema);
	ObjectProviders.Remove(Schema);
}

IDocModDefinitionProvider* UDocModContentSubsystem::FindProvider(const FString& Schema) const
{
	if (const TSharedPtr<IDocModDefinitionProvider>* Found = SharedProviders.Find(Schema))
	{
		return Found->Get();
	}
	if (const TWeakObjectPtr<UObject>* FoundObj = ObjectProviders.Find(Schema))
	{
		if (UObject* Obj = FoundObj->Get())
		{
			return Cast<IDocModDefinitionProvider>(Obj);
		}
	}
	return nullptr;
}


FString UDocModContentSubsystem::ComputeContentHash(const FString& Content)
{
	return FMD5::HashAnsiString(*Content);
}

bool UDocModContentSubsystem::DiscoverPackInDirectory(const FString& DirectoryPath, FDocModManifest& OutManifest, FString& OutError)
{
	FString ManifestPath = FPaths::Combine(DirectoryPath, TEXT("manifest.json"));
	if (!FPaths::FileExists(ManifestPath))
	{
		OutError = FString::Printf(TEXT("manifest.json not found in %s"), *DirectoryPath);
		return false;
	}

	const UDocModContentSettings* Settings = GetDefault<UDocModContentSettings>();
	const int64 ManifestBytes = IFileManager::Get().FileSize(*ManifestPath);
	if (ManifestBytes < 0 || ManifestBytes > Settings->MaxManifestBytes)
	{
		OutError = FString::Printf(TEXT("manifest.json size %lld exceeds limit %lld"), ManifestBytes, Settings->MaxManifestBytes);
		return false; // checked before reading or parsing
	}

	FString JsonContent;
	if (!FFileHelper::LoadFileToString(JsonContent, *ManifestPath))
	{
		OutError = FString::Printf(TEXT("Failed to read %s"), *ManifestPath);
		return false;
	}

	if (!FDocModManifest::ParseFromJson(JsonContent, OutManifest, OutError))
	{
		return false;
	}

	OutManifest.PackDirectoryPath = DirectoryPath;
	return true;
}

EDocModValidationResult UDocModContentSubsystem::ValidateManifest(const FDocModManifest& Manifest, FString& OutError) const
{
	const UDocModContentSettings* Settings = GetDefault<UDocModContentSettings>();
	int32 MaxDefs = Settings ? Settings->MaxDefinitionsPerPack : 1000;
	int32 MaxStrLen = Settings ? Settings->MaxStringLength : 2048;

	const int32 MaxVersion = Settings ? Settings->MaxSupportedManifestVersion : 1;
	if (Manifest.ManifestVersion <= 0 || Manifest.ManifestVersion > MaxVersion)
	{
		OutError = FString::Printf(TEXT("Unsupported manifest_version %d (supported 1..%d)."), Manifest.ManifestVersion, MaxVersion);
		return EDocModValidationResult::InvalidVersion;
	}

	if (Manifest.PackId.IsEmpty() || Manifest.PackId.Len() > MaxStrLen)
	{
		OutError = TEXT("Invalid pack_id length.");
		return EDocModValidationResult::ResourceLimitExceeded;
	}

	const int32 MaxFiles = Settings ? Settings->MaxFilesPerPack : 500;
	if (Manifest.Definitions.Num() > MaxFiles)
	{
		OutError = FString::Printf(TEXT("Exceeded maximum files per pack (%d > %d)."), Manifest.Definitions.Num(), MaxFiles);
		return EDocModValidationResult::ResourceLimitExceeded;
	}

	if (Manifest.Definitions.Num() > MaxDefs)
	{
		OutError = FString::Printf(TEXT("Exceeded maximum allowed definitions (%d > %d)."), Manifest.Definitions.Num(), MaxDefs);
		return EDocModValidationResult::ResourceLimitExceeded;
	}

	// Validate namespacing: all definition IDs should be namespaced by pack_id
	for (const FDocModDefinitionEntry& Def : Manifest.Definitions)
	{
		if (Def.DefinitionId.Len() > MaxStrLen || Def.RelativePath.Len() > MaxStrLen)
		{
			OutError = TEXT("Definition field exceeds maximum string length.");
			return EDocModValidationResult::ResourceLimitExceeded;
		}

		if (!Def.DefinitionId.StartsWith(Manifest.PackId + TEXT(":")))
		{
			OutError = FString::Printf(TEXT("Definition '%s' must be namespaced with pack prefix '%s:'"), *Def.DefinitionId, *Manifest.PackId);
			return EDocModValidationResult::NamespaceCollision;
		}
	}

	return EDocModValidationResult::Valid;
}

EDocModValidationResult UDocModContentSubsystem::ValidatePathSafety(const FString& PackRoot, const FString& RelativePath, FString& OutCanonicalPath, FString& OutError) const
{
	const UDocModContentSettings* Settings = GetDefault<UDocModContentSettings>();

	FString CleanRel = RelativePath.TrimStartAndEnd();
	CleanRel.ReplaceInline(TEXT("\\"), TEXT("/"));

	// Check path traversal patterns
	if (CleanRel.StartsWith(TEXT("/")) || CleanRel.StartsWith(TEXT("\\")) ||
		CleanRel.Contains(TEXT("..")) || CleanRel.Contains(TEXT(":")))
	{
		OutError = FString::Printf(TEXT("Path traversal or absolute path detected in relative path: %s"), *CleanRel);
		return EDocModValidationResult::PathTraversal;
	}

	// Extension security check
	FString Extension = FPaths::GetExtension(CleanRel, true).ToLower();
	if (Settings)
	{
		for (const FString& Blocked : Settings->BlockedExtensions)
		{
			if (Extension.Equals(Blocked.ToLower()))
			{
				OutError = FString::Printf(TEXT("Blocked file extension '%s' detected for path: %s"), *Extension, *CleanRel);
				return EDocModValidationResult::ExecutableContentBlocked;
			}
		}
		if (Settings->AllowedExtensions.Num() > 0 && !Settings->AllowedExtensions.ContainsByPredicate([&Extension](const FString& Allowed) { return Extension.Equals(Allowed.ToLower()); }))
		{
			OutError = FString::Printf(TEXT("Extension '%s' is not in the data allowlist: %s"), *Extension, *CleanRel);
			return EDocModValidationResult::ExecutableContentBlocked;
		}
	}

	FString NormalizedRoot = FPaths::ConvertRelativePathToFull(PackRoot);
	FPaths::NormalizeDirectoryName(NormalizedRoot);

	FString Combined = FPaths::Combine(NormalizedRoot, CleanRel);
	FPaths::CollapseRelativeDirectories(Combined);

	// Require a separator after the root so "Pack" never matches a sibling "PackEvil".
	if (!Combined.StartsWith(NormalizedRoot + TEXT("/")))
	{
		OutError = FString::Printf(TEXT("Resolved path escape outside pack root: %s"), *Combined);
		return EDocModValidationResult::PathTraversal;
	}

	OutCanonicalPath = Combined;
	return EDocModValidationResult::Valid;
}

EDocModValidationResult UDocModContentSubsystem::ValidatePackContent(const FDocModManifest& Manifest, const FString& PackRoot, FString& OutError, TMap<FString, FString>& OutDefinitionHashes)
{
	EDocModValidationResult ManifestRes = ValidateManifest(Manifest, OutError);
	if (ManifestRes != EDocModValidationResult::Valid)
	{
		return ManifestRes;
	}

	const UDocModContentSettings* Settings = GetDefault<UDocModContentSettings>();
	int64 MaxFileSize = Settings ? Settings->MaxFileSize : (10 * 1024 * 1024);
	int64 MaxAggBytes = Settings ? Settings->MaxAggregatePackBytes : (50 * 1024 * 1024);
	int64 CurrentAggBytes = 0;

	OutDefinitionHashes.Empty();
	TSet<FString> SeenPaths;

	for (const FDocModDefinitionEntry& Def : Manifest.Definitions)
	{
		FString CanonicalPath;
		EDocModValidationResult PathRes = ValidatePathSafety(PackRoot, Def.RelativePath, CanonicalPath, OutError);
		if (PathRes != EDocModValidationResult::Valid)
		{
			return PathRes;
		}

		// Case-insensitive file systems would alias these; reject instead of picking one.
		bool bAlias = false;
		SeenPaths.Add(CanonicalPath.ToLower(), &bAlias);
		if (bAlias)
		{
			OutError = FString::Printf(TEXT("Path '%s' collides with another definition path (case-insensitive)"), *Def.RelativePath);
			return EDocModValidationResult::PathTraversal;
		}

		if (!FPaths::FileExists(CanonicalPath))
		{
			OutError = FString::Printf(TEXT("Definition file not found: %s"), *CanonicalPath);
			return EDocModValidationResult::ResourceLimitExceeded;
		}

		int64 FileSize = IFileManager::Get().FileSize(*CanonicalPath);
		if (FileSize < 0 || FileSize > MaxFileSize)
		{
			OutError = FString::Printf(TEXT("Definition file %s exceeds max file size (%lld > %lld)"), *Def.RelativePath, FileSize, MaxFileSize);
			return EDocModValidationResult::ResourceLimitExceeded;
		}

		CurrentAggBytes += FileSize;
		if (CurrentAggBytes > MaxAggBytes)
		{
			OutError = FString::Printf(TEXT("Aggregate pack size exceeded maximum budget (%lld > %lld)"), CurrentAggBytes, MaxAggBytes);
			return EDocModValidationResult::ResourceLimitExceeded;
		}

		FString FileContent;
		if (!FFileHelper::LoadFileToString(FileContent, *CanonicalPath))
		{
			OutError = FString::Printf(TEXT("Failed to load definition file: %s"), *CanonicalPath);
			return EDocModValidationResult::ResourceLimitExceeded;
		}

		// Calculate byte hash
		FString Hash = ComputeContentHash(FileContent);
		OutDefinitionHashes.Add(Def.DefinitionId, Hash);

		// Schema provider validation
		IDocModDefinitionProvider* Provider = FindProvider(Def.Schema);
		if (!Provider)
		{
			OutError = FString::Printf(TEXT("No registered provider for schema '%s' (definition: %s)"), *Def.Schema, *Def.DefinitionId);
			return EDocModValidationResult::UnsupportedSchema;
		}

		if (Def.SchemaVersion <= 0 || Def.SchemaVersion > Provider->GetSupportedSchemaVersion())
		{
			OutError = FString::Printf(TEXT("Definition '%s' schema_version %d is not supported by '%s' (max %d)"),
				*Def.DefinitionId, Def.SchemaVersion, *Def.Schema, Provider->GetSupportedSchemaVersion());
			return EDocModValidationResult::UnsupportedSchema;
		}

		FString ProviderError;
		if (!Provider->ValidateDefinition(Def, FileContent, ProviderError))
		{
			OutError = FString::Printf(TEXT("Provider rejected definition '%s': %s"), *Def.DefinitionId, *ProviderError);
			return EDocModValidationResult::ProviderError;
		}
	}

	return EDocModValidationResult::Valid;
}

bool UDocModContentSubsystem::BuildActivationPlan(const TArray<FDocModManifest>& Packs, FDocModActivationPlan& OutPlan)
{
	OutPlan = FDocModActivationPlan();
	OutPlan.bIsValid = false;

	TMap<FString, const FDocModManifest*> PackMap;
	TSet<FString> SeenPackIds;

	// Check duplicates
	for (const FDocModManifest& Pack : Packs)
	{
		if (SeenPackIds.Contains(Pack.PackId))
		{
			OutPlan.DiagnosticMessages.Add(FString::Printf(TEXT("Duplicate pack ID detected: %s"), *Pack.PackId));
			return false;
		}
		SeenPackIds.Add(Pack.PackId);
		PackMap.Add(Pack.PackId, &Pack);
	}

	// Check namespace collisions across all definitions
	TSet<FString> GlobalDefinitionIds;
	for (const FDocModManifest& Pack : Packs)
	{
		for (const FDocModDefinitionEntry& Def : Pack.Definitions)
		{
			if (GlobalDefinitionIds.Contains(Def.DefinitionId))
			{
				OutPlan.DiagnosticMessages.Add(FString::Printf(TEXT("Namespace collision for definition ID: %s"), *Def.DefinitionId));
				return false;
			}
			GlobalDefinitionIds.Add(Def.DefinitionId);
		}
	}

	// Definitions already owned by another active pack cannot be overwritten silently.
	for (const FDocModManifest& Pack : Packs)
	{
		for (const FDocModDefinitionEntry& Def : Pack.Definitions)
		{
			for (const TPair<FString, FDocModManifest>& Active : ActiveManifests)
			{
				if (Active.Key != Pack.PackId && Active.Value.Definitions.ContainsByPredicate([&Def](const FDocModDefinitionEntry& E) { return E.DefinitionId == Def.DefinitionId; }))
				{
					OutPlan.DiagnosticMessages.Add(FString::Printf(TEXT("Definition '%s' is already provided by active pack '%s'"), *Def.DefinitionId, *Active.Key));
					return false;
				}
			}
		}
	}

	// Conflicts with packs that are already active count too.
	for (const FDocModManifest& Pack : Packs)
	{
		for (const FString& ConflictId : Pack.Conflicts)
		{
			if (CurrentCatalog.ActivePackIds.Contains(ConflictId) && !PackMap.Contains(ConflictId))
			{
				OutPlan.DiagnosticMessages.Add(FString::Printf(TEXT("Pack '%s' conflicts with active pack '%s'"), *Pack.PackId, *ConflictId));
				return false;
			}
		}
		for (const TPair<FString, FDocModManifest>& Active : ActiveManifests)
		{
			if (Active.Key != Pack.PackId && Active.Value.Conflicts.Contains(Pack.PackId))
			{
				OutPlan.DiagnosticMessages.Add(FString::Printf(TEXT("Active pack '%s' declares a conflict with '%s'"), *Active.Key, *Pack.PackId));
				return false;
			}
		}
	}

	// Check conflicts
	for (const FDocModManifest& Pack : Packs)
	{
		for (const FString& ConflictId : Pack.Conflicts)
		{
			if (PackMap.Contains(ConflictId))
			{
				OutPlan.DiagnosticMessages.Add(FString::Printf(TEXT("Conflict detected between pack '%s' and '%s'"), *Pack.PackId, *ConflictId));
				return false;
			}
		}
	}

	// Build dependency graph
	// Dependencies: A requires B -> B must be activated before A (B -> A edge)
	TMap<FString, TSet<FString>> AdjacencyList; // Dep -> Dependent
	TMap<FString, int32> InDegree;

	for (const FDocModManifest& Pack : Packs)
	{
		InDegree.Add(Pack.PackId, 0);
		AdjacencyList.Add(Pack.PackId, TSet<FString>());
	}

	for (const FDocModManifest& Pack : Packs)
	{
		for (const FDocModDependency& Dep : Pack.Requires)
		{
			if (!PackMap.Contains(Dep.PackId))
			{
				if (!Dep.bOptional)
				{
					OutPlan.DiagnosticMessages.Add(FString::Printf(TEXT("Pack '%s' requires missing dependency '%s'"), *Pack.PackId, *Dep.PackId));
					return false;
				}
				continue;
			}

			// Validate semver constraint
			const FDocModManifest* DepPack = PackMap[Dep.PackId];
			if (!DepPack->PackVersion.MatchesConstraint(Dep.VersionConstraint))
			{
				OutPlan.DiagnosticMessages.Add(FString::Printf(TEXT("Pack '%s' requires '%s' version %s, but found %s"),
					*Pack.PackId, *Dep.PackId, *Dep.VersionConstraint, *DepPack->PackVersion.ToString()));
				return false;
			}

			// DepPack must precede Pack
			AdjacencyList[Dep.PackId].Add(Pack.PackId);
			InDegree[Pack.PackId]++;
		}
	}

	// Topological sort (Kahn's algorithm)
	TArray<FString> ZeroDegree;
	for (const auto& Kvp : InDegree)
	{
		if (Kvp.Value == 0)
		{
			ZeroDegree.Add(Kvp.Key);
		}
	}
	ZeroDegree.Sort(); // Deterministic tie-break

	TArray<FString> ResultOrder;
	while (ZeroDegree.Num() > 0)
	{
		FString Current = ZeroDegree[0];
		ZeroDegree.RemoveAt(0);
		ResultOrder.Add(Current);

		if (const TSet<FString>* Dependents = AdjacencyList.Find(Current))
		{
			for (const FString& DepId : *Dependents)
			{
				InDegree[DepId]--;
				if (InDegree[DepId] == 0)
				{
					ZeroDegree.Add(DepId);
					ZeroDegree.Sort(); // Maintain deterministic tie-break
				}
			}
		}
	}

	if (ResultOrder.Num() < Packs.Num())
	{
		OutPlan.DiagnosticMessages.Add(TEXT("Circular dependency detected among mod packs."));
		return false;
	}

	OutPlan.OrderedPackIds = ResultOrder;
	for (int32 Index = 0; Index < ResultOrder.Num(); ++Index)
	{
		OutPlan.LoadOrderExplanations.Add(FString::Printf(TEXT("[%d] %s"), Index, *ResultOrder[Index]));
	}

	// Every pack must pass content validation (paths, extensions, sizes, schema, provider).
	// A pack that fails makes the whole plan invalid; nothing unvalidated is ever activated.
	for (const FDocModManifest& Pack : Packs)
	{
		FString PackErr;
		TMap<FString, FString> Hashes;
		const EDocModValidationResult ContentRes = ValidatePackContent(Pack, Pack.PackDirectoryPath, PackErr, Hashes);
		if (ContentRes != EDocModValidationResult::Valid)
		{
			OutPlan.DiagnosticMessages.Add(FString::Printf(TEXT("Pack '%s' failed validation: %s"), *Pack.PackId, *PackErr));
			OutPlan.OrderedPackIds.Reset();
			OutPlan.ValidatedDefinitionHashes.Reset();
			return false;
		}
		for (const auto& Kvp : Hashes)
		{
			OutPlan.ValidatedDefinitionHashes.Add(Kvp.Key, Kvp.Value);
		}
	}

	OutPlan.bIsValid = true;
	return true;
}

EDocModValidationResult UDocModContentSubsystem::ActivatePlan(const FDocModActivationPlan& Plan, const TArray<FDocModManifest>& Packs)
{
	if (!Plan.bIsValid)
	{
		return EDocModValidationResult::MissingDependency;
	}

	TMap<FString, const FDocModManifest*> PackMap;
	for (const FDocModManifest& Pack : Packs)
	{
		PackMap.Add(Pack.PackId, &Pack);
	}

	// Phase 1: re-check every path, read each file once, and require the validated hash (MOD-06).
	// The bytes read here are the bytes staged below; files are never read a second time.
	const UDocModContentSettings* Settings = GetDefault<UDocModContentSettings>();
	TMap<FString, FString> VerifiedContent; // DefinitionId -> validated bytes
	for (const FString& PackId : Plan.OrderedPackIds)
	{
		const FDocModManifest** FoundPack = PackMap.Find(PackId);
		if (!FoundPack || !*FoundPack)
		{
			return EDocModValidationResult::MissingDependency; // the plan names a pack that was not supplied
		}
		const FDocModManifest* Pack = *FoundPack;

		for (const FDocModDefinitionEntry& Def : Pack->Definitions)
		{
			const FString* ExpectedHash = Plan.ValidatedDefinitionHashes.Find(Def.DefinitionId);
			if (!ExpectedHash)
			{
				return EDocModValidationResult::ByteHashMismatch; // never validated: refuse
			}

			FString FullPath;
			FString PathError;
			const EDocModValidationResult PathRes = ValidatePathSafety(Pack->PackDirectoryPath, Def.RelativePath, FullPath, PathError);
			if (PathRes != EDocModValidationResult::Valid)
			{
				return PathRes;
			}

			const int64 FileSize = IFileManager::Get().FileSize(*FullPath);
			if (FileSize < 0 || FileSize > Settings->MaxFileSize)
			{
				return EDocModValidationResult::ResourceLimitExceeded;
			}

			FString Content;
			if (!FFileHelper::LoadFileToString(Content, *FullPath))
			{
				return EDocModValidationResult::ResourceLimitExceeded;
			}

			if (*ExpectedHash != ComputeContentHash(Content))
			{
				return EDocModValidationResult::ByteHashMismatch;
			}
			VerifiedContent.Add(Def.DefinitionId, MoveTemp(Content));
		}
	}

	// Phase 2: Transactional Staging across providers (MOD-05)
	TArray<IDocModDefinitionProvider*> TouchedProviders;
	for (const FString& PackId : Plan.OrderedPackIds)
	{
		const FDocModManifest** FoundPack = PackMap.Find(PackId);
		if (!FoundPack || !*FoundPack) continue;
		const FDocModManifest* Pack = *FoundPack;

		for (const FDocModDefinitionEntry& Def : Pack->Definitions)
		{
			IDocModDefinitionProvider* Provider = FindProvider(Def.Schema);
			if (!Provider)
			{
				// Rollback all
				for (IDocModDefinitionProvider* P : TouchedProviders)
				{
					P->RollbackStagedDefinitions();
				}
				return EDocModValidationResult::UnsupportedSchema;
			}

			if (!TouchedProviders.Contains(Provider))
			{
				TouchedProviders.Add(Provider);
			}

			const FString& Content = VerifiedContent.FindChecked(Def.DefinitionId);

			FString StageErr;
			if (!Provider->StageDefinition(Def, Content, StageErr))
			{
				// Staging failure: atomic rollback of all staged definitions!
				for (IDocModDefinitionProvider* P : TouchedProviders)
				{
					P->RollbackStagedDefinitions();
				}
				return EDocModValidationResult::ProviderError;
			}
		}
	}

	// Phase 3: All staged successfully -> Commit
	for (IDocModDefinitionProvider* P : TouchedProviders)
	{
		P->CommitStagedDefinitions();
	}

	// Update Catalog revision
	CurrentCatalog.RevisionNumber++;
	CurrentCatalog.Timestamp = FDateTime::UtcNow();

	for (const FString& PackId : Plan.OrderedPackIds)
	{
		if (!CurrentCatalog.ActivePackIds.Contains(PackId))
		{
			CurrentCatalog.ActivePackIds.Add(PackId);
		}

		const FDocModManifest** FoundPack = PackMap.Find(PackId);
		if (FoundPack && *FoundPack)
		{
			ActiveManifests.Add(PackId, **FoundPack);
			for (const FDocModDefinitionEntry& Def : (*FoundPack)->Definitions)
			{
				CurrentCatalog.RegisteredDefinitions.Add(Def.DefinitionId, Def);
			}
		}
	}

	return EDocModValidationResult::Valid;
}

EDocModValidationResult UDocModContentSubsystem::RequestDeactivatePack(const FString& PackId)
{
	if (!CurrentCatalog.ActivePackIds.Contains(PackId))
	{
		return EDocModValidationResult::Valid;
	}

	// Check if in use / pinned (MOD-07)
	if (HasActivePinsForPack(PackId))
	{
		return EDocModValidationResult::InUse;
	}

	const FDocModManifest* Manifest = ActiveManifests.Find(PackId);
	if (Manifest)
	{
		for (const FDocModDefinitionEntry& Def : Manifest->Definitions)
		{
			IDocModDefinitionProvider* Provider = FindProvider(Def.Schema);
			if (Provider)
			{
				Provider->DeactivateDefinition(Def.DefinitionId);
			}
			CurrentCatalog.RegisteredDefinitions.Remove(Def.DefinitionId);
		}
		ActiveManifests.Remove(PackId);
	}

	CurrentCatalog.ActivePackIds.Remove(PackId);
	CurrentCatalog.RevisionNumber++;
	CurrentCatalog.Timestamp = FDateTime::UtcNow();

	return EDocModValidationResult::Valid;
}

FDocModPinHandle UDocModContentSubsystem::AcquireDefinitionPin(const FString& DefinitionId)
{
	FDocModPinHandle Handle;
	Handle.PinId = FGuid::NewGuid();
	Handle.DefinitionId = DefinitionId;
	Handle.AcquiredTime = FDateTime::UtcNow();

	ActivePins.FindOrAdd(DefinitionId).Add(Handle.PinId);
	return Handle;
}

bool UDocModContentSubsystem::ReleaseDefinitionPin(const FDocModPinHandle& PinHandle)
{
	if (!PinHandle.IsValid())
	{
		return false;
	}

	TSet<FGuid>* FoundSet = ActivePins.Find(PinHandle.DefinitionId);
	if (FoundSet)
	{
		bool bRemoved = FoundSet->Remove(PinHandle.PinId) > 0;
		if (FoundSet->Num() == 0)
		{
			ActivePins.Remove(PinHandle.DefinitionId);
		}
		return bRemoved;
	}
	return false;
}

bool UDocModContentSubsystem::IsDefinitionPinned(const FString& DefinitionId) const
{
	const TSet<FGuid>* FoundSet = ActivePins.Find(DefinitionId);
	return FoundSet && FoundSet->Num() > 0;
}

int32 UDocModContentSubsystem::GetActivePinCount(const FString& DefinitionId) const
{
	const TSet<FGuid>* FoundSet = ActivePins.Find(DefinitionId);
	return FoundSet ? FoundSet->Num() : 0;
}

bool UDocModContentSubsystem::HasActivePinsForPack(const FString& PackId) const
{
	FString Prefix = PackId + TEXT(":");
	for (const auto& Kvp : ActivePins)
	{
		if (Kvp.Key.StartsWith(Prefix) && Kvp.Value.Num() > 0)
		{
			return true;
		}
	}
	return false;
}

void UDocModContentSubsystem::QuarantineMissingContent(const FString& PackId, const FString& DefinitionId, const FString& PreservedPayloadJson, const FString& Reason)
{
	FDocModQuarantineRecord Record;
	Record.PackId = PackId;
	Record.DefinitionId = DefinitionId;
	Record.PreservedPayloadJson = PreservedPayloadJson;
	Record.QuarantineReason = Reason;

	QuarantineStore.Add(Record);
}

TArray<FDocModQuarantineRecord> UDocModContentSubsystem::GetQuarantinedRecords() const
{
	return QuarantineStore;
}

void UDocModContentSubsystem::ClearQuarantine()
{
	QuarantineStore.Empty();
}

bool UDocModContentSubsystem::IsPackActive(const FString& PackId) const
{
	return CurrentCatalog.ActivePackIds.Contains(PackId);
}

bool UDocModContentSubsystem::FindDefinition(const FString& DefinitionId, FDocModDefinitionEntry& OutEntry) const
{
	const FDocModDefinitionEntry* Found = CurrentCatalog.RegisteredDefinitions.Find(DefinitionId);
	if (Found)
	{
		OutEntry = *Found;
		return true;
	}
	return false;
}
