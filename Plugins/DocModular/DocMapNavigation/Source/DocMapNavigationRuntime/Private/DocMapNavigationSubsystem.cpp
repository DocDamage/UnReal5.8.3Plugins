#include "DocMapNavigationSubsystem.h"
#include "DocMapMath.h"
#include "DocMapNavigationLog.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocMapNavigationSubsystem)

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

UDocMapMarkerRegistry* UDocMapMarkerRegistry::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocMapMarkerRegistry>() : nullptr;
}

bool UDocMapMarkerRegistry::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDocMapMarkerRegistry::Deinitialize()
{
	Markers.Reset();
	Owners.Reset();
	Registrations.Reset();
	DefinitionRefs.Reset();
	Super::Deinitialize();
}

void UDocMapMarkerRegistry::Changed(const FDocMapMarkerState& State, EDocMarkerChange Change)
{
	++RegistryRevision;
	OnMarkerChangedNative.Broadcast(State, Change);
}

FDocSystemResult UDocMapMarkerRegistry::RegisterMarker(UObject* Source, FName MarkerId, const FGuid& InstanceScope, FName MapId,
	UDocMapMarkerDefinition* Definition, const FVector& EngineLocation, FDocRequestHandle& OutHandle, const FDocOwnerScope& OwnerScope)
{
	OutHandle = FDocRequestHandle();
	if (!Source || MarkerId.IsNone() || !Definition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Source, MarkerId and Definition are required"));
	}
	if (!FMath::IsFinite(EngineLocation.X) || !FMath::IsFinite(EngineLocation.Y) || !FMath::IsFinite(EngineLocation.Z))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Non-finite marker location"));
	}
	const FKey Key{ MarkerId, InstanceScope };
	const FObjectKey SourceKey(Source);
	if (const FObjectKey* Existing = Owners.Find(Key))
	{
		if (*Existing != SourceKey && Existing->ResolveObjectPtr() != nullptr)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
				FString::Printf(TEXT("Marker %s is registered by another source"), *MarkerId.ToString()));
		}
	}
	DefinitionRefs.AddUnique(Definition);
	FDocMapMarkerState* State = Markers.Find(Key);
	const bool bNew = State == nullptr;
	if (!State)
	{
		State = &Markers.Add(Key);
		State->MarkerId = MarkerId;
		State->InstanceScope = InstanceScope;
	}
	State->MapId = MapId;
	State->Definition = Definition;
	State->MarkerTag = Definition->MarkerTag;
	State->Tags = Definition->FilterTags;
	State->Tags.AddTag(Definition->MarkerTag);
	State->Priority = Definition->Priority;
	State->Label = Definition->LabelTemplate;
	State->Lifetime = Definition->Lifetime;
	State->Audience = Definition->Audience;
	State->OwnerScope = OwnerScope;
	State->Location = UDocMapMath::ToAbsolute(GetWorld(), EngineLocation);
	State->SourceKey = GetPathNameSafe(Source);
	State->bHasLiveSource = true;
	State->bStale = false;
	State->ObservedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	++State->Revision;
	Owners.Add(Key, SourceKey);
	OutHandle = Registrations.Add(Source, FRegistration{ Key, SourceKey });
	Changed(*State, bNew ? EDocMarkerChange::Added : EDocMarkerChange::Updated);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocMapMarkerRegistry::UnregisterMarker(const FDocRequestHandle& Handle, UObject* Source)
{
	FRegistration Removed;
	if (!Registrations.Remove(Handle, Source, &Removed))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Not registered (already removed?)"));
	}
	Owners.Remove(Removed.Key);
	FDocMapMarkerState* State = Markers.Find(Removed.Key);
	if (!State)
	{
		return FDocSystemResult::MakeSuccess();
	}
	State->bHasLiveSource = false;
	switch (State->Lifetime)
	{
	case EDocMarkerLifetime::ActorLifetime:
	{
		const FDocMapMarkerState Copy = *State;
		Markers.Remove(Removed.Key);
		Changed(Copy, EDocMarkerChange::Removed);
		break;
	}
	case EDocMarkerLifetime::PersistentLocation:
		++State->Revision;
		Changed(*State, EDocMarkerChange::Updated);
		break;
	case EDocMarkerLifetime::LastKnownDynamic:
		State->bStale = true; // last authorized location; not a live tracking feed
		++State->Revision;
		Changed(*State, EDocMarkerChange::Stale);
		break;
	case EDocMarkerLifetime::ProviderManaged:
		State->bStale = true; // never invent a current position
		++State->Revision;
		Changed(*State, EDocMarkerChange::Stale);
		OnProviderAttentionNative.Broadcast(*State, EDocMarkerChange::Stale);
		break;
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocMapMarkerRegistry::UpdateMarker(const FDocRequestHandle& Handle, UObject* Source, const FVector& EngineLocation, float Yaw, int64 ExpectedRevision)
{
	const FRegistration* Registration = Registrations.Find(Handle, Source);
	FDocMapMarkerState* State = Registration ? Markers.Find(Registration->Key) : nullptr;
	if (!State)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Registration not active"));
	}
	if (ExpectedRevision >= 0 && ExpectedRevision != State->Revision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("Stale revision %lld (current %lld)"), ExpectedRevision, State->Revision));
	}
	const FVector Absolute = UDocMapMath::ToAbsolute(GetWorld(), EngineLocation);
	if (Absolute.Equals(State->Location, 0.01) && FMath::IsNearlyEqual(Yaw, State->Yaw))
	{
		return FDocSystemResult::MakeNoChange();
	}
	State->Location = Absolute;
	State->Yaw = Yaw;
	State->ObservedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	++State->Revision;
	Changed(*State, EDocMarkerChange::Updated);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocMapMarkerRegistry::SetMarkerVisibility(FName MarkerId, const FGuid& InstanceScope, bool bVisible)
{
	FDocMapMarkerState* State = Markers.Find(FKey{ MarkerId, InstanceScope });
	if (!State)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown marker"));
	}
	if (State->bSharedVisible == bVisible)
	{
		return FDocSystemResult::MakeNoChange();
	}
	State->bSharedVisible = bVisible;
	++State->Revision;
	Changed(*State, EDocMarkerChange::Updated);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocMapMarkerRegistry::RemovePersistentMarker(FName MarkerId, const FGuid& InstanceScope)
{
	const FKey Key{ MarkerId, InstanceScope };
	FDocMapMarkerState Copy;
	if (!Markers.RemoveAndCopyValue(Key, Copy))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already removed"));
	}
	Owners.Remove(Key);
	Registrations.RemoveIf([&Key](const FDocRequestHandle&, const FRegistration& R) { return R.Key == Key; });
	Changed(Copy, EDocMarkerChange::Removed);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocMapMarkerRegistry::ProviderUpdate(FName MarkerId, const FGuid& InstanceScope, const FVector& EngineLocation, bool bRemove)
{
	FDocMapMarkerState* State = Markers.Find(FKey{ MarkerId, InstanceScope });
	if (!State || State->Lifetime != EDocMarkerLifetime::ProviderManaged)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No provider-managed marker with this id"));
	}
	if (bRemove)
	{
		return RemovePersistentMarker(MarkerId, InstanceScope);
	}
	State->Location = UDocMapMath::ToAbsolute(GetWorld(), EngineLocation);
	State->bStale = false;
	State->ObservedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	++State->Revision;
	Changed(*State, EDocMarkerChange::Updated);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocMapMarkerRegistry::AddCustomMarker(FName MarkerId, FName MapId, UDocMapMarkerDefinition* Definition, const FVector& EngineLocation, const FDocOwnerScope& OwnerScope)
{
	if (MarkerId.IsNone() || !Definition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("MarkerId and Definition required"));
	}
	const FKey Key{ MarkerId, FGuid() };
	if (Markers.Contains(Key))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Marker id already exists"));
	}
	DefinitionRefs.AddUnique(Definition);
	FDocMapMarkerState& State = Markers.Add(Key);
	State.MarkerId = MarkerId;
	State.MapId = MapId;
	State.Definition = Definition;
	State.MarkerTag = Definition->MarkerTag;
	State.Tags = Definition->FilterTags;
	State.Tags.AddTag(Definition->MarkerTag);
	State.Priority = Definition->Priority;
	State.Label = Definition->LabelTemplate;
	State.Lifetime = EDocMarkerLifetime::PersistentLocation;
	State.Audience = Definition->Audience;
	State.OwnerScope = OwnerScope;
	State.Location = UDocMapMath::ToAbsolute(GetWorld(), EngineLocation);
	State.bCustom = true;
	State.Revision = 1;
	Changed(State, EDocMarkerChange::Added);
	return FDocSystemResult::MakeSuccess();
}

bool UDocMapMarkerRegistry::GetMarker(FName MarkerId, const FGuid& InstanceScope, FDocMapMarkerState& Out) const
{
	if (const FDocMapMarkerState* State = Markers.Find(FKey{ MarkerId, InstanceScope }))
	{
		Out = *State;
		return true;
	}
	return false;
}

TArray<FDocMapMarkerState> UDocMapMarkerRegistry::Snapshot() const
{
	TArray<FDocMapMarkerState> Out;
	Markers.GenerateValueArray(Out);
	return Out;
}

TArray<FDocMapMarkerState> UDocMapMarkerRegistry::CapturePersistent() const
{
	TArray<FDocMapMarkerState> Out;
	for (const TPair<FKey, FDocMapMarkerState>& Pair : Markers)
	{
		if (Pair.Value.bCustom || Pair.Value.Lifetime == EDocMarkerLifetime::PersistentLocation || Pair.Value.Lifetime == EDocMarkerLifetime::LastKnownDynamic)
		{
			FDocMapMarkerState Copy = Pair.Value;
			Copy.bHasLiveSource = false;
			Copy.SourceKey.Reset();
			Out.Add(Copy);
		}
	}
	Out.Sort([](const FDocMapMarkerState& A, const FDocMapMarkerState& B) { return A.MarkerId.LexicalLess(B.MarkerId); });
	return Out;
}

void UDocMapMarkerRegistry::RestorePersistent(const TArray<FDocMapMarkerState>& Records)
{
	for (const FDocMapMarkerState& Record : Records)
	{
		const FKey Key{ Record.MarkerId, Record.InstanceScope };
		if (Owners.Contains(Key))
		{
			continue; // a live source is authoritative for its own marker
		}
		FDocMapMarkerState Restored = Record;
		Restored.bHasLiveSource = false;
		if (!Restored.Definition)
		{
			Restored.bSharedVisible = false; // quarantined: unknown definition kept, not shown
		}
		else
		{
			DefinitionRefs.AddUnique(Restored.Definition);
		}
		Markers.Add(Key, Restored);
	}
	++RegistryRevision; // state refreshed; no Added events replayed as new gameplay
}

// ---------------------------------------------------------------------------
// Marker component
// ---------------------------------------------------------------------------

UDocMapMarkerComponent::UDocMapMarkerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // static markers never tick
}

void UDocMapMarkerComponent::BeginPlay()
{
	Super::BeginPlay();
	UDocMapMarkerRegistry* Registry = UDocMapMarkerRegistry::Get(this);
	if (!Registry)
	{
		return;
	}
	const FName Id = MarkerId.IsNone() && GetOwner() ? GetOwner()->GetFName() : MarkerId;
	LastSent = GetComponentLocation();
	RegistrationResult = Registry->RegisterMarker(this, Id, InstanceScope, MapId, Definition, LastSent, Registration);
	if (!RegistrationResult.IsSuccess())
	{
		UE_LOG(LogDocMapNavigation, Warning, TEXT("%s: marker not registered: %s"), *GetPathName(), *RegistrationResult.ToString());
	}
	if (bDynamic && Registration.IsSet())
	{
		SetComponentTickInterval(Definition ? Definition->UpdateInterval : 0.25f);
		SetComponentTickEnabled(true);
	}
}

void UDocMapMarkerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDocMapMarkerRegistry* Registry = UDocMapMarkerRegistry::Get(this))
	{
		Registry->UnregisterMarker(Registration, this);
	}
	Registration.Invalidate();
	Super::EndPlay(EndPlayReason);
}

void UDocMapMarkerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const float Threshold = Definition ? Definition->MoveThreshold : 100.f;
	const FVector Now = GetComponentLocation();
	if (FVector::DistSquared(Now, LastSent) < FMath::Square(Threshold))
	{
		return;
	}
	if (UDocMapMarkerRegistry* Registry = UDocMapMarkerRegistry::Get(this))
	{
		Registry->UpdateMarker(Registration, this, Now, GetComponentRotation().Yaw);
		LastSent = Now;
	}
}

// ---------------------------------------------------------------------------
// Local player subsystem
// ---------------------------------------------------------------------------

UDocMapNavigationSubsystem* UDocMapNavigationSubsystem::Get(const ULocalPlayer* Player)
{
	return Player ? Player->GetSubsystem<UDocMapNavigationSubsystem>() : nullptr;
}

UWorld* UDocMapNavigationSubsystem::GetWorld() const
{
	if (UWorld* Override = WorldOverride.Get())
	{
		return Override;
	}
	const ULocalPlayer* Player = GetLocalPlayer();
	return Player ? Player->GetWorld() : nullptr;
}

void UDocMapNavigationSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);
	// World-specific view data is rebound after travel; owner-scoped discovery stays.
	for (TPair<FName, FMapState>& Pair : States)
	{
		Pair.Value.CurrentFloor = NAME_None;
	}
}

void UDocMapNavigationSubsystem::RegisterMap(UDocMapDefinition* Map)
{
	if (Map && !Map->MapId.IsNone())
	{
		MapsById.Add(Map->MapId, Map);
		MapRefs.AddUnique(Map);
		StateFor(Map->MapId).CellSize = Map->DiscoveryCellSize;
	}
}

UDocMapDefinition* UDocMapNavigationSubsystem::FindMap(FName MapId) const
{
	const TObjectPtr<UDocMapDefinition>* Found = MapsById.Find(MapId);
	return Found ? Found->Get() : nullptr;
}

UDocMapNavigationSubsystem::FMapState& UDocMapNavigationSubsystem::StateFor(FName MapId)
{
	FMapState& State = States.FindOrAdd(MapId);
	if (const UDocMapDefinition* Map = FindMap(MapId))
	{
		if (State.Chunks.Num() == 0) { State.CellSize = Map->DiscoveryCellSize; }
	}
	return State;
}

bool UDocMapNavigationSubsystem::CellOf(const UDocMapDefinition* Map, const FVector& World, FIntPoint& OutCell) const
{
	const FDocMapPoint Point = UDocMapMath::WorldToNormalized(Map->Transform, World);
	if (!Point.IsValid())
	{
		return false;
	}
	const double Size = Map->DiscoveryCellSize;
	OutCell = FIntPoint(FMath::FloorToInt(Point.Normalized.X * Map->Transform.LengthU / Size), FMath::FloorToInt(Point.Normalized.Y * Map->Transform.LengthV / Size));
	return true;
}

bool UDocMapNavigationSubsystem::IsMarkerRevealed(const FDocMapMarkerState& M, const UDocMapDefinition* Map) const
{
	if (!M.Definition || !M.Definition->bRequiresDiscovery)
	{
		return true;
	}
	if (!M.RegionId.IsNone() && IsLocationDiscovered(M.MapId, M.RegionId))
	{
		return true;
	}
	return Map && IsPointRevealed(M.MapId, UDocMapMath::ToEngine(GetWorld(), M.Location));
}

bool UDocMapNavigationSubsystem::PassesFilters(const FDocMapMarkerState& M, const FDocMarkerQuery& Query, const UDocMapDefinition* Map) const
{
	// Order: audience → map/layer → floor → discovery → tags → distance (priority/budget after).
	if (M.Audience == EDocMarkerAudience::OwnerOnly && M.OwnerScope != OwnerScope)
	{
		return false;
	}
	if (!M.bSharedVisible || Hidden.Contains(M.MarkerId))
	{
		return false;
	}
	if (!Query.MapId.IsNone() && M.MapId != Query.MapId)
	{
		return false;
	}
	if (!Query.Layers.IsEmpty() && !(M.Definition && Query.Layers.HasTag(M.Definition->Layer)))
	{
		return false;
	}
	if (!Query.FloorId.IsNone())
	{
		if (M.FloorId.IsNone() ? !Query.bIncludeFloorless : M.FloorId != Query.FloorId)
		{
			return false;
		}
	}
	if (!Query.bIncludeUndiscovered && !IsMarkerRevealed(M, Map))
	{
		return false;
	}
	if (!Query.RequiredTags.IsEmpty() && !(Query.bExactTags ? M.Tags.HasAllExact(Query.RequiredTags) : M.Tags.HasAll(Query.RequiredTags)))
	{
		return false;
	}
	if (!HiddenTags.IsEmpty() && M.Tags.HasAny(HiddenTags))
	{
		return false;
	}
	if (Query.bUseDistance && Query.MaxDistance > 0.0)
	{
		const FVector Engine = UDocMapMath::ToEngine(GetWorld(), M.Location);
		const FVector Delta = Engine - Query.Origin;
		const double D = Query.b3DDistance ? Delta.Size() : FVector2D(Delta.X, Delta.Y).Size();
		if (D > Query.MaxDistance)
		{
			return false;
		}
	}
	return true;
}

TArray<FDocMapMarkerState> UDocMapNavigationSubsystem::GetVisibleMarkers(const FDocMarkerQuery& Query) const
{
	TArray<FDocMapMarkerState> Out;
	const UDocMapMarkerRegistry* Registry = UDocMapMarkerRegistry::Get(GetWorld());
	if (!Registry)
	{
		return Out;
	}
	const UDocMapDefinition* Map = FindMap(Query.MapId);
	for (const FDocMapMarkerState& M : Registry->Snapshot())
	{
		if (PassesFilters(M, Query, Map)) { Out.Add(M); }
	}
	// Deterministic: tracked first, then priority, then stable id (never hash order).
	Out.Sort([this](const FDocMapMarkerState& A, const FDocMapMarkerState& B)
	{
		const bool TA = Tracked.Contains(A.MarkerId), TB = Tracked.Contains(B.MarkerId);
		if (TA != TB) { return TA; }
		if (A.Priority != B.Priority) { return A.Priority > B.Priority; }
		if (A.MarkerId != B.MarkerId) { return A.MarkerId.LexicalLess(B.MarkerId); }
		return A.InstanceScope.ToString() < B.InstanceScope.ToString();
	});
	const int32 Max = Query.MaxResults > 0 ? Query.MaxResults : GetDefault<UDocMapNavigationSettings>()->DefaultMaxResults;
	if (Out.Num() > Max) { Out.SetNum(Max); }
	return Out;
}

TArray<FDocMapMarkerState> UDocMapNavigationSubsystem::GetMarkersByTag(FName MapId, FGameplayTag Tag, bool bExact) const
{
	FDocMarkerQuery Query;
	Query.MapId = MapId;
	Query.RequiredTags.AddTag(Tag);
	Query.bExactTags = bExact;
	return GetVisibleMarkers(Query);
}

bool UDocMapNavigationSubsystem::GetNearestMarker(const FDocMarkerQuery& Query, FDocMapMarkerState& Out) const
{
	FDocMarkerQuery Unbounded = Query;
	Unbounded.MaxResults = TNumericLimits<int32>::Max();
	double Best = TNumericLimits<double>::Max();
	bool bFound = false;
	for (const FDocMapMarkerState& M : GetVisibleMarkers(Unbounded))
	{
		const FVector Delta = UDocMapMath::ToEngine(GetWorld(), M.Location) - Query.Origin;
		const double D = Query.b3DDistance ? Delta.Size() : FVector2D(Delta.X, Delta.Y).Size();
		if (D < Best || (D == Best && bFound && M.MarkerId.LexicalLess(Out.MarkerId)))
		{
			Best = D;
			Out = M;
			bFound = true;
		}
	}
	return bFound;
}

void UDocMapNavigationSubsystem::SetMarkerHidden(FName MarkerId, bool bHidden)
{
	if (bHidden) { Hidden.Add(MarkerId); } else { Hidden.Remove(MarkerId); }
}

void UDocMapNavigationSubsystem::SetMarkerTracked(FName MarkerId, bool bTracked)
{
	if (bTracked) { Tracked.Add(MarkerId); } else { Tracked.Remove(MarkerId); }
}

void UDocMapNavigationSubsystem::SetTagFilter(FGameplayTag Tag, bool bHidden)
{
	if (bHidden) { HiddenTags.AddTag(Tag); } else { HiddenTags.RemoveTag(Tag); }
}

FDocSystemResult UDocMapNavigationSubsystem::SetWaypoint(FName MapId, FVector Location, FName FloorId)
{
	if (!FMath::IsFinite(Location.X) || !FMath::IsFinite(Location.Y) || !FMath::IsFinite(Location.Z))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Non-finite waypoint"));
	}
	StateFor(MapId).Waypoint = TPair<FVector, FName>(UDocMapMath::ToAbsolute(GetWorld(), Location), FloorId);
	return FDocSystemResult::MakeSuccess();
}

void UDocMapNavigationSubsystem::ClearWaypoint(FName MapId)
{
	if (FMapState* State = States.Find(MapId)) { State->Waypoint.Reset(); }
}

bool UDocMapNavigationSubsystem::GetWaypoint(FName MapId, FVector& OutLocation, FName& OutFloor) const
{
	const FMapState* State = States.Find(MapId);
	if (!State || !State->Waypoint.IsSet())
	{
		return false;
	}
	OutLocation = UDocMapMath::ToEngine(GetWorld(), State->Waypoint->Key);
	OutFloor = State->Waypoint->Value;
	return true;
}

// ---- Discovery ----

FDocSystemResult UDocMapNavigationSubsystem::DiscoverLocation(FName MapId, FName LocationId)
{
	if (MapId.IsNone() || LocationId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("MapId and LocationId required"));
	}
	bool bAlready = false;
	StateFor(MapId).Locations.Add(LocationId, &bAlready);
	if (bAlready)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already discovered")); // idempotent: no duplicate reward
	}
	OnLocationDiscovered.Broadcast(MapId, LocationId);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocMapNavigationSubsystem::UndiscoverLocation(FName MapId, FName LocationId)
{
	FMapState* State = States.Find(MapId);
	return (State && State->Locations.Remove(LocationId) > 0) ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeNoChange();
}

bool UDocMapNavigationSubsystem::IsLocationDiscovered(FName MapId, FName LocationId) const
{
	const FMapState* State = States.Find(MapId);
	return State && State->Locations.Contains(LocationId);
}

TArray<FName> UDocMapNavigationSubsystem::GetDiscoveredLocations(FName MapId) const
{
	TArray<FName> Out;
	if (const FMapState* State = States.Find(MapId)) { Out = State->Locations.Array(); }
	Out.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	return Out;
}

namespace DocMapPrivate
{
	FIntPoint FloorDiv(const FIntPoint& Cell, int32 Size)
	{
		auto Div = [Size](int32 V) { return V >= 0 ? V / Size : -((-V + Size - 1) / Size); };
		return FIntPoint(Div(Cell.X), Div(Cell.Y));
	}
}

int32 UDocMapNavigationSubsystem::RevealRadius(FName MapId, FVector WorldLocation, double Radius)
{
	const UDocMapDefinition* Map = FindMap(MapId);
	FIntPoint Center;
	if (!Map || Radius <= 0.0 || !CellOf(Map, WorldLocation, Center))
	{
		return 0;
	}
	FMapState& State = StateFor(MapId);
	const int32 ChunkCells = GetDefault<UDocMapNavigationSettings>()->ChunkCells;
	const double Size = Map->DiscoveryCellSize;
	const FDocMapPoint P = UDocMapMath::WorldToNormalized(Map->Transform, WorldLocation);
	const FVector2D Pos(P.Normalized.X * Map->Transform.LengthU, P.Normalized.Y * Map->Transform.LengthV);
	const int32 Reach = FMath::CeilToInt(Radius / Size) + 1;
	int32 Revealed = 0;
	for (int32 X = Center.X - Reach; X <= Center.X + Reach; ++X)
	{
		for (int32 Y = Center.Y - Reach; Y <= Center.Y + Reach; ++Y)
		{
			// Documented rule: a cell is revealed when its centre is within the radius.
			const FVector2D CellCenter((X + 0.5) * Size, (Y + 0.5) * Size);
			if (FVector2D::DistSquared(CellCenter, Pos) > Radius * Radius) { continue; }
			const FIntPoint ChunkKey = DocMapPrivate::FloorDiv(FIntPoint(X, Y), ChunkCells);
			FChunk& Chunk = State.Chunks.FindOrAdd(ChunkKey);
			if (Chunk.Cells.Num() == 0) { Chunk.Cells.Init(false, ChunkCells * ChunkCells); }
			const int32 LX = X - ChunkKey.X * ChunkCells;
			const int32 LY = Y - ChunkKey.Y * ChunkCells;
			const int32 Bit = LY * ChunkCells + LX;
			if (!Chunk.Cells[Bit]) { Chunk.Cells[Bit] = true; ++Revealed; }
		}
	}
	return Revealed;
}

bool UDocMapNavigationSubsystem::IsPointRevealed(FName MapId, FVector WorldLocation) const
{
	const UDocMapDefinition* Map = FindMap(MapId);
	const FMapState* State = States.Find(MapId);
	FIntPoint Cell;
	if (!Map || !State || !CellOf(Map, WorldLocation, Cell))
	{
		return false;
	}
	const int32 ChunkCells = GetDefault<UDocMapNavigationSettings>()->ChunkCells;
	const FIntPoint ChunkKey = DocMapPrivate::FloorDiv(Cell, ChunkCells);
	const FChunk* Chunk = State->Chunks.Find(ChunkKey);
	if (!Chunk || Chunk->Cells.Num() == 0)
	{
		return false;
	}
	return Chunk->Cells[(Cell.Y - ChunkKey.Y * ChunkCells) * ChunkCells + (Cell.X - ChunkKey.X * ChunkCells)];
}

int32 UDocMapNavigationSubsystem::ResetRevealedArea(FName MapId, FVector WorldLocation, double Radius)
{
	const UDocMapDefinition* Map = FindMap(MapId);
	FMapState* State = States.Find(MapId);
	FIntPoint Center;
	if (!Map || !State || !CellOf(Map, WorldLocation, Center))
	{
		return 0;
	}
	const int32 ChunkCells = GetDefault<UDocMapNavigationSettings>()->ChunkCells;
	const double Size = Map->DiscoveryCellSize;
	const FDocMapPoint P = UDocMapMath::WorldToNormalized(Map->Transform, WorldLocation);
	const FVector2D Pos(P.Normalized.X * Map->Transform.LengthU, P.Normalized.Y * Map->Transform.LengthV);
	const int32 Reach = FMath::CeilToInt(Radius / Size) + 1;
	int32 Cleared = 0;
	for (int32 X = Center.X - Reach; X <= Center.X + Reach; ++X)
	{
		for (int32 Y = Center.Y - Reach; Y <= Center.Y + Reach; ++Y)
		{
			const FVector2D CellCenter((X + 0.5) * Size, (Y + 0.5) * Size);
			if (FVector2D::DistSquared(CellCenter, Pos) > Radius * Radius) { continue; }
			const FIntPoint ChunkKey = DocMapPrivate::FloorDiv(FIntPoint(X, Y), ChunkCells);
			FChunk* Chunk = State->Chunks.Find(ChunkKey);
			if (!Chunk || Chunk->Cells.Num() == 0) { continue; }
			const int32 Bit = (Y - ChunkKey.Y * ChunkCells) * ChunkCells + (X - ChunkKey.X * ChunkCells);
			if (Chunk->Cells[Bit]) { Chunk->Cells[Bit] = false; ++Cleared; }
		}
	}
	return Cleared;
}

int32 UDocMapNavigationSubsystem::GetAllocatedChunkCount(FName MapId) const
{
	const FMapState* State = States.Find(MapId);
	return State ? State->Chunks.Num() : 0;
}

// ---- Floors, compass, indicators ----

FName UDocMapNavigationSubsystem::UpdateFloor(FName MapId, double WorldZ)
{
	FMapState& State = StateFor(MapId);
	if (!State.FloorOverride.IsNone())
	{
		State.CurrentFloor = State.FloorOverride;
		return State.CurrentFloor;
	}
	const UDocMapDefinition* Map = FindMap(MapId);
	State.CurrentFloor = Map ? UDocMapMath::ResolveFloor(Map->Floors, WorldZ, State.CurrentFloor, Map->FloorHysteresis) : NAME_None;
	return State.CurrentFloor;
}

void UDocMapNavigationSubsystem::SetFloorOverride(FName MapId, FName FloorId)
{
	StateFor(MapId).FloorOverride = FloorId;
}

TArray<FDocCompassEntry> UDocMapNavigationSubsystem::GetCompass(const FDocMarkerQuery& Query, FVector From, float HeadingYaw) const
{
	TArray<FDocCompassEntry> Out;
	const UDocMapDefinition* Map = FindMap(Query.MapId);
	const float North = Map ? Map->NorthYawDegrees : 0.f;
	for (const FDocMapMarkerState& M : GetVisibleMarkers(Query))
	{
		FDocCompassEntry Entry;
		if (UDocMapMath::ComputeBearing(From, HeadingYaw, UDocMapMath::ToEngine(GetWorld(), M.Location), North, Entry))
		{
			Entry.MarkerId = M.MarkerId;
			Entry.Priority = M.Priority;
			Out.Add(Entry);
		}
	}
	return Out; // empty for an invalid heading (never a guessed direction)
}

TArray<FDocOffscreenIndicator> UDocMapNavigationSubsystem::GetOffscreenIndicators(const FDocMarkerQuery& Query, const FDocMapViewProjection& View) const
{
	TArray<FDocOffscreenIndicator> Out;
	for (const FDocMapMarkerState& M : GetVisibleMarkers(Query))
	{
		FDocOffscreenIndicator Indicator;
		if (UDocMapMath::ProjectIndicator(View, UDocMapMath::ToEngine(GetWorld(), M.Location), Indicator))
		{
			Indicator.MarkerId = M.MarkerId;
			Out.Add(Indicator);
		}
	}
	return Out;
}

// ---- Persistence ----

namespace DocMapPrivate
{
	constexpr uint32 Magic = 0x50414D44; // "DMAP"

	/** Bounded reader: array counts in corrupt data cannot allocate beyond the limit. */
	class FBoundedReader : public FMemoryReader
	{
	public:
		FBoundedReader(const TArray<uint8>& Bytes, int64 Max) : FMemoryReader(Bytes) { ArMaxSerializeSize = Max; }
	};
}

TArray<uint8> UDocMapNavigationSubsystem::CaptureState() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes);
	uint32 Magic = DocMapPrivate::Magic;
	int32 Version = SchemaVersion;
	int32 ChunkCells = GetDefault<UDocMapNavigationSettings>()->ChunkCells;
	FDocOwnerScope Scope = OwnerScope;
	Ar << Magic << Version << ChunkCells << Scope;
	TArray<FName> MapIds;
	States.GetKeys(MapIds);
	MapIds.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	int32 NumMaps = MapIds.Num();
	Ar << NumMaps;
	for (FName MapId : MapIds)
	{
		const FMapState& S = States[MapId];
		double CellSize = S.CellSize;
		TArray<FName> Locations = S.Locations.Array();
		Ar << MapId << CellSize << Locations;
		int32 NumChunks = S.Chunks.Num();
		Ar << NumChunks;
		for (const TPair<FIntPoint, FChunk>& Chunk : S.Chunks)
		{
			FIntPoint Key = Chunk.Key;
			TArray<uint8> Packed;
			Packed.SetNumZeroed((Chunk.Value.Cells.Num() + 7) / 8);
			for (int32 i = 0; i < Chunk.Value.Cells.Num(); ++i) { if (Chunk.Value.Cells[i]) { Packed[i / 8] |= (1 << (i % 8)); } }
			Ar << Key << Packed;
		}
		bool bWaypoint = S.Waypoint.IsSet();
		Ar << bWaypoint;
		if (bWaypoint)
		{
			FVector Loc = S.Waypoint->Key;
			FName Floor = S.Waypoint->Value;
			Ar << Loc << Floor;
		}
	}
	TArray<FName> HiddenList = Hidden.Array();
	TArray<FName> TrackedList = Tracked.Array();
	Ar << HiddenList << TrackedList;
	return Bytes;
}

FDocSystemResult UDocMapNavigationSubsystem::RestoreState(const TArray<uint8>& Bytes, bool bDiscardIncompatibleFog)
{
	DocMapPrivate::FBoundedReader Ar(Bytes, 16 * 1024 * 1024);
	uint32 Magic = 0;
	int32 Version = 0, ChunkCells = 0, NumMaps = 0;
	FDocOwnerScope Scope;
	Ar << Magic << Version;
	if (Ar.IsError() || Magic != DocMapPrivate::Magic)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Not a map navigation state"));
	}
	if (Version > SchemaVersion)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("State from a newer schema"));
	}
	Ar << ChunkCells << Scope << NumMaps;
	if (Ar.IsError() || NumMaps < 0 || NumMaps > 4096)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Corrupt map state"));
	}
	if (OwnerScope.IsValid() && Scope != OwnerScope)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("State belongs to a different owner scope"));
	}
	const int32 CurrentChunkCells = GetDefault<UDocMapNavigationSettings>()->ChunkCells;
	// Stage everything before touching live state.
	TMap<FName, FMapState> Staged;
	for (int32 m = 0; m < NumMaps; ++m)
	{
		FName MapId;
		double CellSize = 0;
		TArray<FName> Locations;
		int32 NumChunks = 0;
		Ar << MapId << CellSize << Locations << NumChunks;
		if (Ar.IsError() || NumChunks < 0 || NumChunks > 1 << 20)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Corrupt map state"));
		}
		FMapState S;
		S.CellSize = CellSize;
		S.Locations = TSet<FName>(Locations);
		const UDocMapDefinition* Map = FindMap(MapId);
		const bool bFogCompatible = ChunkCells == CurrentChunkCells && (!Map || FMath::IsNearlyEqual(Map->DiscoveryCellSize, CellSize));
		for (int32 c = 0; c < NumChunks; ++c)
		{
			FIntPoint Key;
			TArray<uint8> Packed;
			Ar << Key << Packed;
			if (Ar.IsError()) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Corrupt chunk")); }
			if (!bFogCompatible) { continue; }
			FChunk Chunk;
			Chunk.Cells.Init(false, ChunkCells * ChunkCells);
			for (int32 i = 0; i < Chunk.Cells.Num() && i / 8 < Packed.Num(); ++i) { Chunk.Cells[i] = (Packed[i / 8] >> (i % 8)) & 1; }
			S.Chunks.Add(Key, MoveTemp(Chunk));
		}
		if (!bFogCompatible && NumChunks > 0 && !bDiscardIncompatibleFog)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
				FString::Printf(TEXT("Fog schema for %s changed; conversion or explicit reset confirmation required"), *MapId.ToString()));
		}
		bool bWaypoint = false;
		Ar << bWaypoint;
		if (bWaypoint)
		{
			FVector Loc;
			FName Floor;
			Ar << Loc << Floor;
			S.Waypoint = TPair<FVector, FName>(Loc, Floor);
		}
		Staged.Add(MapId, MoveTemp(S));
	}
	TArray<FName> HiddenList, TrackedList;
	Ar << HiddenList << TrackedList;
	if (Ar.IsError())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Corrupt map state"));
	}
	// Apply (no discovery events are replayed: restoration is not new gameplay).
	States = MoveTemp(Staged);
	Hidden = TSet<FName>(HiddenList);
	Tracked = TSet<FName>(TrackedList);
	if (!OwnerScope.IsValid()) { OwnerScope = Scope; }
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Discovery component
// ---------------------------------------------------------------------------

UDocMapDiscoveryComponent::UDocMapDiscoveryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.5f;
}

void UDocMapDiscoveryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	SetComponentTickInterval(IntervalSeconds);
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UDocMapNavigationSubsystem* Map = PC && PC->IsLocalController() ? UDocMapNavigationSubsystem::Get(PC->GetLocalPlayer()) : nullptr;
	if (!Map)
	{
		return; // discovery is per local player; no local player → nothing (never player 0)
	}
	const UDocMapDefinition* Definition = Map->FindMap(MapId);
	Map->RevealRadius(MapId, GetOwner()->GetActorLocation(), Radius > 0.f ? Radius : (Definition ? Definition->DefaultRevealRadius : 5000.0));
}
